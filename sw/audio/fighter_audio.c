#include "fighter_audio.h"

/*
 * 音频后端实现。
 *
 * 这个文件同时支持三种播放方式：
 * 1. disabled：无音频，便于没有音频环境时运行游戏逻辑。
 * 2. command：调用系统播放器（aplay/ffplay/afplay）播放 wav。
 * 3. MMIO：通过 /dev/mem 映射 FPGA 上的 WM8731 音频 IP 寄存器，把采样
 *    直接写入硬件 FIFO。
 *
 * MMIO 寄存器均按 32-bit word 访问，因为 FPGA 侧 Avalon-MM 数据宽度为
 * 32 bit，HPS 端使用 volatile uint32_t 可保证每次都是实际硬件访问。
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum {
  FIGHTER_PLAYER_KIND_NONE = 0,
  FIGHTER_PLAYER_KIND_APLAY,
  FIGHTER_PLAYER_KIND_FFPLAY,
  FIGHTER_PLAYER_KIND_AFPLAY
};

enum {
  FIGHTER_AUDIO_TRACK_STORAGE_COUNT = FIGHTER_AUDIO_TRACK_GAME_OVER + 1
};

enum {
  FIGHTER_AUDIO_MMIO_TARGET_RATE = 48000,
  /* CONTROL bit2/bit3：硬件约定的清 FIFO 控制位，寄存器本身仍是 32 bit。 */
  FIGHTER_AUDIO_MMIO_CONTROL_CLEAR_READ = 1 << 2,
  FIGHTER_AUDIO_MMIO_CONTROL_CLEAR_WRITE = 1 << 3
};

/*
 * 音频 IP 的 32-bit MMIO 寄存器表：
 * 0 CONTROL   读状态/写清除位。
 * 1 FIFOSPACE 读左右 FIFO 剩余空间，高两个字节分别代表左右声道。
 * 2 LEFTDATA  写左声道 sample word。
 * 3 RIGHTDATA 写右声道 sample word。
 *
 * 位宽为什么都是 32 bit：这是 Avalon-MM slave 的数据总线宽度。音频有效
 * 采样是 int16，但软件写入时左移 16 位放到高半字，配合 WM8731
 * left-justified 16-bit 串行格式；低 16 bit 保留为 0。
 */
enum {
  FIGHTER_AUDIO_MMIO_REG_CONTROL = 0,
  FIGHTER_AUDIO_MMIO_REG_FIFOSPACE = 1,
  FIGHTER_AUDIO_MMIO_REG_LEFTDATA = 2,
  FIGHTER_AUDIO_MMIO_REG_RIGHTDATA = 3,
  FIGHTER_AUDIO_MMIO_REG_COUNT = 4
};

typedef struct {
  /* samples 以 interleaved stereo int16 保存：L,R,L,R... */
  int16_t *samples;
  size_t frame_count;
} fighter_audio_clip_t;

typedef struct {
  pthread_t thread;
  pthread_mutex_t mutex;
  int mutex_initialized;
  int thread_started;
  int stop_requested;
  int mem_fd;
  /* bridge_reset_reg 是 Cyclone V HPS bridge reset 寄存器，32 bit 宽；
   * 清 bit[1:0] 使能 HPS-to-FPGA / lightweight HPS-to-FPGA bridge。 */
  void *bridge_map;
  size_t bridge_map_length;
  volatile uint32_t *bridge_reset_reg;
  /* audio_regs 指向自定义音频 IP 的 4 个 32-bit word 寄存器。 */
  void *audio_map;
  size_t audio_map_length;
  volatile uint32_t *audio_regs;
  fighter_audio_clip_t clips[FIGHTER_AUDIO_TRACK_STORAGE_COUNT];
  fighter_audio_track_t current_track;
  fighter_audio_track_t loop_track;
  size_t current_frame;
  int playing;
} fighter_audio_mmio_state_t;

static const off_t k_fighter_audio_default_bridge_reset_addr = (off_t)0xFFD0501C;
static const off_t k_fighter_audio_default_mmio_addr = (off_t)0xFF200000;

/* 写入音频上下文状态字符串，用于向主程序解释当前后端状态或错误。 */
static void fighter_audio_set_status_detail(fighter_audio_context_t *context,
                                            const char *fmt,
                                            ...) {
  va_list args;

  if (!context || !fmt) {
    return;
  }

  va_start(args, fmt);
  vsnprintf(context->status_detail, sizeof(context->status_detail), fmt, args);
  va_end(args);
}

/* 在 PATH 中寻找播放器可执行文件，返回解析后的路径。 */
static const char *fighter_find_in_path(const char *name) {
  static char resolved_path[512];
  const char *path_env;
  const char *segment;

  if (!name || strchr(name, '/') != NULL) {
    return NULL;
  }

  path_env = getenv("PATH");
  if (!path_env) {
    return NULL;
  }

  segment = path_env;
  while (*segment != '\0') {
    const char *separator = strchr(segment, ':');
    size_t prefix_len =
        separator ? (size_t)(separator - segment) : strlen(segment);

    if (prefix_len + 1 + strlen(name) + 1 < sizeof(resolved_path)) {
      memcpy(resolved_path, segment, prefix_len);
      resolved_path[prefix_len] = '/';
      strcpy(resolved_path + prefix_len + 1, name);
      if (access(resolved_path, X_OK) == 0) {
        return resolved_path;
      }
    }

    if (!separator) {
      break;
    }
    segment = separator + 1;
  }

  return NULL;
}

/* 回收已退出的子进程，避免命令播放器留下僵尸进程。 */
static void fighter_audio_reap_children(void) {
  while (waitpid(-1, NULL, WNOHANG) > 0) {
  }
}

/* 对 shell 命令参数做单引号转义，避免路径中的特殊字符破坏命令。 */
static int fighter_audio_shell_quote(const char *src, char *dst, size_t size) {
  size_t used = 0;

  if (!src || !dst || size < 3) {
    return -1;
  }

  dst[used++] = '\'';
  while (*src != '\0') {
    if (*src == '\'') {
      if (used + 4 >= size) {
        return -1;
      }
      memcpy(dst + used, "'\\''", 4);
      used += 4;
    } else {
      if (used + 1 >= size) {
        return -1;
      }
      dst[used++] = *src;
    }
    ++src;
  }

  if (used + 2 > size) {
    return -1;
  }
  dst[used++] = '\'';
  dst[used] = '\0';
  return 0;
}

/* fork 后通过 /bin/sh -c 异步执行播放命令。 */
static int fighter_audio_spawn_shell(const char *command) {
  pid_t pid;

  if (!command) {
    return -1;
  }

  pid = fork();
  if (pid < 0) {
    return -1;
  }

  if (pid == 0) {
    setsid();
    execl("/bin/sh", "sh", "-c", command, (char *)NULL);
    _exit(127);
  }

  return (int)pid;
}

/* 安全复制字符串，确保目标缓冲区以 NUL 结尾。 */
static int fighter_audio_copy_string(const char *src, char *dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0) {
    return -1;
  }

  if (strlen(src) + 1 > dst_size) {
    return -1;
  }

  memcpy(dst, src, strlen(src) + 1);
  return 0;
}

/* 从环境变量解析物理地址，未设置时使用默认 MMIO 地址。 */
static int fighter_audio_parse_env_address(const char *env_name,
                                           off_t default_value,
                                           off_t *value_out) {
  const char *text;
  char *end;
  unsigned long long parsed;

  if (!env_name || !value_out) {
    return -1;
  }

  text = getenv(env_name);
  if (!text || text[0] == '\0') {
    *value_out = default_value;
    return 0;
  }

  errno = 0;
  parsed = strtoull(text, &end, 0);
  if (errno != 0 || end == text || !end || *end != '\0') {
    return -1;
  }

  *value_out = (off_t)parsed;
  return 0;
}

/* 在 Linux 上探测可用的 aplay 设备名，优先使用环境指定设备。 */
static int fighter_audio_detect_aplay_device(char *buffer, size_t buffer_size) {
  const char *override;
  FILE *stream;
  char line[256];
  int card;
  int device;

  if (!buffer || buffer_size == 0 || !fighter_find_in_path("aplay")) {
    return -1;
  }

  override = getenv("FIGHTER_AUDIO_DEVICE");
  if (override && override[0] != '\0') {
    return fighter_audio_copy_string(override, buffer, buffer_size);
  }

  stream = popen("aplay -l 2>/dev/null", "r");
  if (!stream) {
    return -1;
  }

  while (fgets(line, sizeof(line), stream)) {
    if (sscanf(line, "card %d: %*[^,], device %d:", &card, &device) == 2) {
      snprintf(buffer, buffer_size, "plughw:%d,%d", card, device);
      (void)pclose(stream);
      return 0;
    }
  }

  (void)pclose(stream);
  return -1;
}

/* 构造播放一次音效的 shell 命令。 */
static void fighter_audio_build_once_command(const fighter_audio_context_t *context,
                                             const char *quoted_path,
                                             char *buffer,
                                             size_t buffer_size) {
  char quoted_device[128];

  if (!context) {
    buffer[0] = '\0';
    return;
  }

  switch (context->player_kind) {
    case FIGHTER_PLAYER_KIND_APLAY:
      if (fighter_audio_shell_quote(context->aplay_device, quoted_device,
                                    sizeof(quoted_device)) != 0) {
        buffer[0] = '\0';
        break;
      }
      snprintf(buffer, buffer_size, "aplay -q -D %s %s >/dev/null 2>&1",
               quoted_device, quoted_path);
      break;
    case FIGHTER_PLAYER_KIND_FFPLAY:
      snprintf(buffer, buffer_size,
               "ffplay -nodisp -autoexit -loglevel quiet %s >/dev/null 2>&1",
               quoted_path);
      break;
    case FIGHTER_PLAYER_KIND_AFPLAY:
      snprintf(buffer, buffer_size, "afplay %s >/dev/null 2>&1", quoted_path);
      break;
    default:
      buffer[0] = '\0';
      break;
  }
}

/* 构造循环播放 BGM 的 shell 命令。 */
static void fighter_audio_build_loop_command(const fighter_audio_context_t *context,
                                             const char *quoted_path,
                                             char *buffer,
                                             size_t buffer_size) {
  char quoted_device[128];

  if (!context) {
    buffer[0] = '\0';
    return;
  }

  switch (context->player_kind) {
    case FIGHTER_PLAYER_KIND_APLAY:
      if (fighter_audio_shell_quote(context->aplay_device, quoted_device,
                                    sizeof(quoted_device)) != 0) {
        buffer[0] = '\0';
        break;
      }
      snprintf(buffer, buffer_size,
               "while aplay -q -D %s %s >/dev/null 2>&1; do :; done",
               quoted_device, quoted_path);
      break;
    case FIGHTER_PLAYER_KIND_FFPLAY:
      snprintf(buffer, buffer_size,
               "ffplay -nodisp -autoexit -loglevel quiet -loop 0 %s "
               ">/dev/null 2>&1",
               quoted_path);
      break;
    case FIGHTER_PLAYER_KIND_AFPLAY:
      snprintf(buffer, buffer_size,
               "while afplay %s >/dev/null 2>&1; do :; done", quoted_path);
      break;
    default:
      buffer[0] = '\0';
      break;
  }
}

/* 选择系统播放器并准备 command 音频后端。 */
static int fighter_audio_prepare_command_backend(fighter_audio_context_t *context) {
  if (!context) {
    return -1;
  }

  context->aplay_device[0] = '\0';
  if (fighter_audio_detect_aplay_device(context->aplay_device,
                                        sizeof(context->aplay_device)) == 0) {
    context->player_kind = FIGHTER_PLAYER_KIND_APLAY;
    return 0;
  }

  if (fighter_find_in_path("ffplay")) {
    context->player_kind = FIGHTER_PLAYER_KIND_FFPLAY;
    return 0;
  }

  if (fighter_find_in_path("afplay")) {
    context->player_kind = FIGHTER_PLAYER_KIND_AFPLAY;
    return 0;
  }

  context->player_kind = FIGHTER_PLAYER_KIND_NONE;
  return -1;
}

/* 释放一个已加载 WAV clip 的 sample 缓冲。 */
static void fighter_audio_clip_reset(fighter_audio_clip_t *clip) {
  if (!clip) {
    return;
  }

  free(clip->samples);
  clip->samples = NULL;
  clip->frame_count = 0;
}

/* 从字节流读取 little-endian 16-bit 数值。 */
static uint16_t fighter_audio_read_le16(const unsigned char *src) {
  return (uint16_t)src[0] | (uint16_t)((uint16_t)src[1] << 8);
}

/* 从字节流读取 little-endian 32-bit 数值。 */
static uint32_t fighter_audio_read_le32(const unsigned char *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

/* 把源 PCM16 数据重采样/混合为 48kHz stereo int16。 */
static int fighter_audio_resample_pcm16(fighter_audio_clip_t *clip,
                                        const int16_t *src_samples,
                                        size_t src_frame_count,
                                        uint16_t src_channels,
                                        uint32_t src_rate) {
  int16_t *dst_samples;
  size_t dst_frame_count;
  size_t i;

  if (!clip || !src_samples || src_frame_count == 0 || src_rate == 0 ||
      (src_channels != 1 && src_channels != 2)) {
    return -1;
  }

  dst_frame_count =
      (size_t)(((uint64_t)src_frame_count * FIGHTER_AUDIO_MMIO_TARGET_RATE +
                src_rate - 1) /
               src_rate);
  if (dst_frame_count == 0) {
    dst_frame_count = 1;
  }

  dst_samples =
      (int16_t *)malloc(dst_frame_count * 2U * sizeof(int16_t));
  if (!dst_samples) {
    return -1;
  }

  for (i = 0; i < dst_frame_count; ++i) {
    uint64_t src_position_num = (uint64_t)i * src_rate;
    size_t src_index = (size_t)(src_position_num / FIGHTER_AUDIO_MMIO_TARGET_RATE);
    uint32_t frac =
        (uint32_t)(src_position_num % FIGHTER_AUDIO_MMIO_TARGET_RATE);
    size_t next_index;
    int channel;

    if (src_index >= src_frame_count) {
      src_index = src_frame_count - 1;
    }
    next_index =
        src_index + 1 < src_frame_count ? src_index + 1 : src_index;

    for (channel = 0; channel < 2; ++channel) {
      int src_channel = src_channels == 1 ? 0 : channel;
      int32_t sample_a =
          src_samples[src_index * src_channels + (size_t)src_channel];
      int32_t sample_b =
          src_samples[next_index * src_channels + (size_t)src_channel];
      int32_t blended =
          sample_a +
          (int32_t)(((int64_t)(sample_b - sample_a) * frac) /
                    FIGHTER_AUDIO_MMIO_TARGET_RATE);
      dst_samples[i * 2U + (size_t)channel] = (int16_t)blended;
    }
  }

  clip->samples = dst_samples;
  clip->frame_count = dst_frame_count;
  return 0;
}

/* 读取 WAV 文件，解析 fmt/data chunk，并装载为内部 PCM clip。 */
static int fighter_audio_clip_load_wav(fighter_audio_clip_t *clip,
                                       const char *path) {
  FILE *stream;
  unsigned char header[12];
  unsigned char chunk_header[8];
  unsigned char *data_bytes = NULL;
  size_t data_size = 0;
  uint16_t audio_format = 0;
  uint16_t channel_count = 0;
  uint16_t bits_per_sample = 0;
  uint32_t sample_rate = 0;
  int have_format = 0;
  int have_data = 0;
  int result = -1;

  if (!clip || !path) {
    return -1;
  }

  stream = fopen(path, "rb");
  if (!stream) {
    return -1;
  }

  if (fread(header, 1, sizeof(header), stream) != sizeof(header) ||
      memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
    fclose(stream);
    return -1;
  }

  while (fread(chunk_header, 1, sizeof(chunk_header), stream) ==
         sizeof(chunk_header)) {
    uint32_t chunk_size = fighter_audio_read_le32(chunk_header + 4);

    if (memcmp(chunk_header, "fmt ", 4) == 0) {
      unsigned char *fmt_data;

      if (chunk_size < 16) {
        break;
      }
      fmt_data = (unsigned char *)malloc(chunk_size);
      if (!fmt_data) {
        break;
      }
      if (fread(fmt_data, 1, chunk_size, stream) != chunk_size) {
        free(fmt_data);
        break;
      }

      audio_format = fighter_audio_read_le16(fmt_data);
      channel_count = fighter_audio_read_le16(fmt_data + 2);
      sample_rate = fighter_audio_read_le32(fmt_data + 4);
      bits_per_sample = fighter_audio_read_le16(fmt_data + 14);
      have_format = 1;
      free(fmt_data);
    } else if (memcmp(chunk_header, "data", 4) == 0) {
      data_bytes = (unsigned char *)malloc(chunk_size);
      if (!data_bytes) {
        break;
      }
      if (fread(data_bytes, 1, chunk_size, stream) != chunk_size) {
        free(data_bytes);
        data_bytes = NULL;
        break;
      }
      data_size = chunk_size;
      have_data = 1;
    } else {
      if (fseek(stream, (long)chunk_size, SEEK_CUR) != 0) {
        break;
      }
    }

    if ((chunk_size & 1U) != 0U) {
      if (fseek(stream, 1L, SEEK_CUR) != 0) {
        break;
      }
    }

    if (have_format && have_data) {
      size_t frame_count;

      if (audio_format != 1 || bits_per_sample != 16 ||
          (channel_count != 1 && channel_count != 2) || sample_rate == 0) {
        break;
      }

      frame_count = data_size / ((size_t)channel_count * sizeof(int16_t));
      if (frame_count == 0) {
        break;
      }

      fighter_audio_clip_reset(clip);
      result = fighter_audio_resample_pcm16(
          clip, (const int16_t *)data_bytes, frame_count, channel_count,
          sample_rate);
      break;
    }
  }

  free(data_bytes);
  fclose(stream);
  return result;
}

/* 用 /dev/mem 映射音频 IP 或 bridge reset 物理寄存器。 */
static int fighter_audio_map_physical(int mem_fd,
                                      off_t physical_addr,
                                      size_t span,
                                      void **map_base,
                                      size_t *map_length,
                                      volatile uint32_t **register_base) {
  long page_size;
  off_t page_base;
  off_t page_offset;
  size_t length;
  void *mapped;

  if (mem_fd < 0 || !map_base || !map_length || !register_base || span == 0) {
    return -1;
  }

  page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    return -1;
  }

  page_base = physical_addr & ~((off_t)page_size - 1);
  page_offset = physical_addr - page_base;
  length = (size_t)page_offset + span;
  length = (length + (size_t)page_size - 1U) & ~((size_t)page_size - 1U);

  mapped = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd,
                page_base);
  if (mapped == MAP_FAILED) {
    return -1;
  }

  *map_base = mapped;
  *map_length = length;
  *register_base =
      (volatile uint32_t *)((unsigned char *)mapped + (size_t)page_offset);
  return 0;
}

/* 解除 MMIO mmap 区域并清空映射记录。 */
static void fighter_audio_mmio_unmap_region(void **map_base, size_t *map_length) {
  if (!map_base || !map_length || !*map_base || *map_length == 0) {
    return;
  }

  munmap(*map_base, *map_length);
  *map_base = NULL;
  *map_length = 0;
}

/* 释放 MMIO 后端已加载的所有音频 clip。 */
static void fighter_audio_mmio_reset_clips(fighter_audio_mmio_state_t *state) {
  int i;

  if (!state) {
    return;
  }

  for (i = 0; i < FIGHTER_AUDIO_TRACK_STORAGE_COUNT; ++i) {
    fighter_audio_clip_reset(&state->clips[i]);
  }
}

/* 为 MMIO 后端预加载菜单 BGM、确认音和 game over 音效。 */
static int fighter_audio_mmio_load_clips(fighter_audio_mmio_state_t *state) {
  int track;

  if (!state) {
    return -1;
  }

  for (track = 1; track < FIGHTER_AUDIO_TRACK_STORAGE_COUNT; ++track) {
    if (fighter_audio_clip_load_wav(&state->clips[track],
                                    fighter_audio_track_path(
                                        (fighter_audio_track_t)track)) != 0) {
      fighter_audio_mmio_reset_clips(state);
      return -1;
    }
  }

  return 0;
}

/* 向音频 IP control 寄存器写清 FIFO 位，重置硬件播放缓冲。 */
static void fighter_audio_mmio_clear_fifos(fighter_audio_mmio_state_t *state) {
  if (!state || !state->audio_regs) {
    return;
  }

  state->audio_regs[FIGHTER_AUDIO_MMIO_REG_CONTROL] =
      FIGHTER_AUDIO_MMIO_CONTROL_CLEAR_READ |
      FIGHTER_AUDIO_MMIO_CONTROL_CLEAR_WRITE;
  state->audio_regs[FIGHTER_AUDIO_MMIO_REG_CONTROL] = 0;
}

/* 读取音频 IP fifospace 状态，确认 MMIO 映射到了可响应的硬件。 */
static int fighter_audio_mmio_probe(fighter_audio_mmio_state_t *state,
                                    uint32_t *fifospace_out) {
  uint32_t fifospace;
  uint32_t write_space_left;
  uint32_t write_space_right;

  if (!state || !state->audio_regs) {
    return -1;
  }

  fighter_audio_mmio_clear_fifos(state);
  fifospace = state->audio_regs[FIGHTER_AUDIO_MMIO_REG_FIFOSPACE];
  if (fifospace_out) {
    *fifospace_out = fifospace;
  }
  write_space_left = (fifospace >> 24) & 0xFFU;
  write_space_right = (fifospace >> 16) & 0xFFU;

  if (write_space_left == 0 || write_space_left > 128 ||
      write_space_right == 0 || write_space_right > 128) {
    return -1;
  }

  return 0;
}

/* 清 HPS bridge reset bit[1:0]，允许 HPS 访问 FPGA 音频 IP。 */
static int fighter_audio_mmio_enable_bridges(fighter_audio_mmio_state_t *state) {
  uint32_t value;

  if (!state || !state->bridge_reset_reg) {
    return -1;
  }

  value = *state->bridge_reset_reg;
  value &= ~0x3U;
  *state->bridge_reset_reg = value;
  return 0;
}

/* 检查 track 枚举是否落在可播放音频资源范围内。 */
static int fighter_audio_track_is_valid(fighter_audio_track_t track) {
  return track > FIGHTER_AUDIO_TRACK_NONE &&
         track <= FIGHTER_AUDIO_TRACK_GAME_OVER;
}

/* 在持锁状态下切换 MMIO 播放 track，并从第 0 帧开始播放。 */
static void fighter_audio_mmio_set_track_locked(fighter_audio_mmio_state_t *state,
                                                fighter_audio_track_t track,
                                                int loop_enabled) {
  if (!state) {
    return;
  }

  if (!fighter_audio_track_is_valid(track) ||
      !state->clips[track].samples || state->clips[track].frame_count == 0) {
    state->current_track = FIGHTER_AUDIO_TRACK_NONE;
    state->loop_track = FIGHTER_AUDIO_TRACK_NONE;
    state->current_frame = 0;
    state->playing = 0;
    return;
  }

  state->current_track = track;
  state->loop_track = loop_enabled ? track : FIGHTER_AUDIO_TRACK_NONE;
  state->current_frame = 0;
  state->playing = 1;
}

/* 在持锁状态下停止 MMIO 播放并清空当前 track。 */
static void fighter_audio_mmio_stop_locked(fighter_audio_mmio_state_t *state) {
  if (!state) {
    return;
  }

  state->current_track = FIGHTER_AUDIO_TRACK_NONE;
  state->loop_track = FIGHTER_AUDIO_TRACK_NONE;
  state->current_frame = 0;
  state->playing = 0;
}

/* 在持锁状态下根据 fifospace 把 PCM sample 填入左右声道硬件 FIFO。 */
static void fighter_audio_mmio_fill_fifo_locked(fighter_audio_mmio_state_t *state) {
  fighter_audio_clip_t *clip;
  uint32_t fifospace;
  size_t writable_frames;
  size_t frame_index;

  if (!state || !state->audio_regs || !state->playing ||
      !fighter_audio_track_is_valid(state->current_track)) {
    return;
  }

  clip = &state->clips[state->current_track];
  if (!clip->samples || clip->frame_count == 0) {
    fighter_audio_mmio_stop_locked(state);
    return;
  }

  fifospace = state->audio_regs[FIGHTER_AUDIO_MMIO_REG_FIFOSPACE];
  writable_frames = (size_t)((fifospace >> 24) & 0xFFU);
  if (((fifospace >> 16) & 0xFFU) < writable_frames) {
    writable_frames = (size_t)((fifospace >> 16) & 0xFFU);
  }

  for (frame_index = 0; frame_index < writable_frames; ++frame_index) {
    int16_t left_sample;
    int16_t right_sample;

    if (state->current_frame >= clip->frame_count) {
      if (state->loop_track == state->current_track) {
        state->current_frame = 0;
      } else {
        fighter_audio_mmio_stop_locked(state);
        break;
      }
    }

    left_sample = clip->samples[state->current_frame * 2U];
    right_sample = clip->samples[state->current_frame * 2U + 1U];
    state->audio_regs[FIGHTER_AUDIO_MMIO_REG_LEFTDATA] =
        (uint32_t)((int32_t)left_sample * 65536);
    state->audio_regs[FIGHTER_AUDIO_MMIO_REG_RIGHTDATA] =
        (uint32_t)((int32_t)right_sample * 65536);
    state->current_frame++;
  }
}

/* MMIO 音频线程入口：循环填充硬件 FIFO，直到收到 stop 请求。 */
static void *fighter_audio_mmio_thread_main(void *opaque) {
  fighter_audio_mmio_state_t *state = (fighter_audio_mmio_state_t *)opaque;
  struct timespec delay;

  delay.tv_sec = 0;
  delay.tv_nsec = 1000000L;

  while (1) {
    pthread_mutex_lock(&state->mutex);
    if (state->stop_requested) {
      pthread_mutex_unlock(&state->mutex);
      break;
    }
    fighter_audio_mmio_fill_fifo_locked(state);
    pthread_mutex_unlock(&state->mutex);
    nanosleep(&delay, NULL);
  }

  return NULL;
}

/* 销毁 MMIO 音频后端：停止线程、释放资源、关闭映射和文件描述符。 */
static void fighter_audio_mmio_destroy(fighter_audio_mmio_state_t *state) {
  if (!state) {
    return;
  }

  if (state->thread_started) {
    pthread_mutex_lock(&state->mutex);
    state->stop_requested = 1;
    pthread_mutex_unlock(&state->mutex);
    pthread_join(state->thread, NULL);
  }

  fighter_audio_mmio_clear_fifos(state);
  fighter_audio_mmio_reset_clips(state);
  fighter_audio_mmio_unmap_region(&state->audio_map, &state->audio_map_length);
  fighter_audio_mmio_unmap_region(&state->bridge_map, &state->bridge_map_length);
  if (state->mem_fd >= 0) {
    close(state->mem_fd);
  }
  if (state->mutex_initialized) {
    pthread_mutex_destroy(&state->mutex);
  }
  free(state);
}

/* 初始化 MMIO 音频后端，包括 /dev/mem、bridge、寄存器映射、资源和线程。 */
static int fighter_audio_mmio_init(fighter_audio_context_t *context) {
  fighter_audio_mmio_state_t *state;
  off_t bridge_reset_addr;
  off_t mmio_addr;
  uint32_t fifospace = 0;
  int thread_create_result;

  if (!context) {
    return -1;
  }

  state = (fighter_audio_mmio_state_t *)calloc(1, sizeof(*state));
  if (!state) {
    return -1;
  }

  state->mem_fd = -1;
  if (pthread_mutex_init(&state->mutex, NULL) != 0) {
    fighter_audio_mmio_destroy(state);
    return -1;
  }
  state->mutex_initialized = 1;

  if (fighter_audio_parse_env_address("FIGHTER_AUDIO_BRIDGE_RESET_ADDR",
                                      k_fighter_audio_default_bridge_reset_addr,
                                      &bridge_reset_addr) != 0) {
    fighter_audio_set_status_detail(
        context,
        "WM8731 MMIO: invalid FIGHTER_AUDIO_BRIDGE_RESET_ADDR=%s",
        getenv("FIGHTER_AUDIO_BRIDGE_RESET_ADDR"));
    fighter_audio_mmio_destroy(state);
    return -1;
  }
  if (fighter_audio_parse_env_address("FIGHTER_AUDIO_MMIO_ADDR",
                                      k_fighter_audio_default_mmio_addr,
                                      &mmio_addr) != 0) {
    fighter_audio_set_status_detail(
        context, "WM8731 MMIO: invalid FIGHTER_AUDIO_MMIO_ADDR=%s",
        getenv("FIGHTER_AUDIO_MMIO_ADDR"));
    fighter_audio_mmio_destroy(state);
    return -1;
  }

  context->bridge_reset_addr = (unsigned long)bridge_reset_addr;
  context->mmio_addr = (unsigned long)mmio_addr;

  state->mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (state->mem_fd < 0) {
    fighter_audio_set_status_detail(context, "WM8731 MMIO: open /dev/mem failed: %s",
                                    strerror(errno));
    fighter_audio_mmio_destroy(state);
    return -1;
  }

  if (fighter_audio_map_physical(state->mem_fd, bridge_reset_addr,
                                 sizeof(uint32_t), &state->bridge_map,
                                 &state->bridge_map_length,
                                 &state->bridge_reset_reg) != 0) {
    fighter_audio_set_status_detail(context,
                                    "WM8731 MMIO: map bridge reset 0x%08lX failed: %s",
                                    (unsigned long)bridge_reset_addr,
                                    strerror(errno));
    fighter_audio_mmio_destroy(state);
    return -1;
  }
  if (fighter_audio_mmio_enable_bridges(state) != 0) {
    fighter_audio_set_status_detail(context,
                                    "WM8731 MMIO: failed to enable FPGA bridges");
    fighter_audio_mmio_destroy(state);
    return -1;
  }

  if (fighter_audio_map_physical(state->mem_fd, mmio_addr,
                                 FIGHTER_AUDIO_MMIO_REG_COUNT *
                                     sizeof(uint32_t),
                                 &state->audio_map,
                                 &state->audio_map_length,
                                 &state->audio_regs) != 0) {
    fighter_audio_set_status_detail(context,
                                    "WM8731 MMIO: map audio core 0x%08lX failed: %s",
                                    (unsigned long)mmio_addr,
                                    strerror(errno));
    fighter_audio_mmio_destroy(state);
    return -1;
  }
  if (fighter_audio_mmio_probe(state, &fifospace) != 0) {
    fighter_audio_set_status_detail(
        context,
        "WM8731 MMIO: audio FIFO probe failed at 0x%08lX (fifospace=0x%08" PRIX32 ")",
        (unsigned long)mmio_addr, fifospace);
    fighter_audio_mmio_destroy(state);
    return -1;
  }

  if (fighter_audio_mmio_load_clips(state) != 0) {
    fighter_audio_set_status_detail(context,
                                    "WM8731 MMIO: failed to load WAV assets");
    fighter_audio_mmio_destroy(state);
    return -1;
  }

  thread_create_result =
      pthread_create(&state->thread, NULL, fighter_audio_mmio_thread_main, state);
  if (thread_create_result != 0) {
    fighter_audio_set_status_detail(context,
                                    "WM8731 MMIO: audio thread start failed: %s",
                                    strerror(thread_create_result));
    fighter_audio_mmio_destroy(state);
    return -1;
  }
  state->thread_started = 1;

  context->backend = FIGHTER_AUDIO_BACKEND_MMIO;
  context->status_detail[0] = '\0';
  context->backend_data = state;
  return 0;
}

/* command 后端停止循环 BGM，结束之前启动的播放器进程。 */
static void fighter_audio_stop_loop_command(fighter_audio_context_t *context) {
  pid_t pid;

  if (!context || context->loop_pid <= 0) {
    return;
  }

  pid = (pid_t)context->loop_pid;
  kill(-pid, SIGTERM);
  waitpid(pid, NULL, 0);
  context->loop_pid = 0;
  context->looping_track = FIGHTER_AUDIO_TRACK_NONE;
}

/* command 后端开始循环播放指定 track。 */
static void fighter_audio_start_loop_command(fighter_audio_context_t *context,
                                             fighter_audio_track_t track) {
  const char *path;
  char quoted_path[512];
  char command[768];
  int pid;

  if (!context || context->backend != FIGHTER_AUDIO_BACKEND_COMMAND) {
    return;
  }

  if (context->looping_track == track && context->loop_pid > 0) {
    return;
  }

  path = fighter_audio_track_path(track);
  if (!path || fighter_audio_shell_quote(path, quoted_path,
                                         sizeof(quoted_path)) != 0) {
    return;
  }

  fighter_audio_build_loop_command(context, quoted_path, command,
                                   sizeof(command));
  if (command[0] == '\0') {
    return;
  }

  fighter_audio_stop_loop_command(context);
  pid = fighter_audio_spawn_shell(command);
  if (pid > 0) {
    context->loop_pid = pid;
    context->looping_track = track;
  }
}

/* command 后端播放一次指定音效。 */
static void fighter_audio_play_once_command(fighter_audio_context_t *context,
                                            fighter_audio_track_t track) {
  const char *path;
  char quoted_path[512];
  char command[768];

  if (!context || context->backend != FIGHTER_AUDIO_BACKEND_COMMAND) {
    return;
  }

  path = fighter_audio_track_path(track);
  if (!path || fighter_audio_shell_quote(path, quoted_path,
                                         sizeof(quoted_path)) != 0) {
    return;
  }

  fighter_audio_build_once_command(context, quoted_path, command,
                                   sizeof(command));
  if (command[0] == '\0') {
    return;
  }

  (void)fighter_audio_spawn_shell(command);
}

/* MMIO 后端停止循环播放并清硬件 FIFO。 */
static void fighter_audio_stop_loop_mmio(fighter_audio_context_t *context) {
  fighter_audio_mmio_state_t *state;

  if (!context || context->backend != FIGHTER_AUDIO_BACKEND_MMIO ||
      !context->backend_data) {
    return;
  }

  state = (fighter_audio_mmio_state_t *)context->backend_data;
  pthread_mutex_lock(&state->mutex);
  fighter_audio_mmio_stop_locked(state);
  fighter_audio_mmio_clear_fifos(state);
  pthread_mutex_unlock(&state->mutex);
  context->looping_track = FIGHTER_AUDIO_TRACK_NONE;
}

/* MMIO 后端切换到指定循环 track。 */
static void fighter_audio_start_loop_mmio(fighter_audio_context_t *context,
                                          fighter_audio_track_t track) {
  fighter_audio_mmio_state_t *state;

  if (!context || context->backend != FIGHTER_AUDIO_BACKEND_MMIO ||
      !context->backend_data) {
    return;
  }

  state = (fighter_audio_mmio_state_t *)context->backend_data;
  pthread_mutex_lock(&state->mutex);
  fighter_audio_mmio_set_track_locked(state, track, 1);
  fighter_audio_mmio_clear_fifos(state);
  fighter_audio_mmio_fill_fifo_locked(state);
  pthread_mutex_unlock(&state->mutex);
  context->looping_track = track;
}

/* MMIO 后端播放一次短音效；当前实现通过切换 track 方式触发。 */
static void fighter_audio_play_once_mmio(fighter_audio_context_t *context,
                                         fighter_audio_track_t track) {
  fighter_audio_mmio_state_t *state;

  if (!context || context->backend != FIGHTER_AUDIO_BACKEND_MMIO ||
      !context->backend_data) {
    return;
  }

  state = (fighter_audio_mmio_state_t *)context->backend_data;
  pthread_mutex_lock(&state->mutex);
  fighter_audio_mmio_set_track_locked(state, track, 0);
  fighter_audio_mmio_clear_fifos(state);
  fighter_audio_mmio_fill_fifo_locked(state);
  pthread_mutex_unlock(&state->mutex);
  context->looping_track = FIGHTER_AUDIO_TRACK_NONE;
}

/* 根据当前后端分派“开始循环播放”命令。 */
static void fighter_audio_start_loop(fighter_audio_context_t *context,
                                     fighter_audio_track_t track) {
  if (!context) {
    return;
  }

  if (context->backend == FIGHTER_AUDIO_BACKEND_MMIO) {
    fighter_audio_start_loop_mmio(context, track);
  } else if (context->backend == FIGHTER_AUDIO_BACKEND_COMMAND) {
    fighter_audio_start_loop_command(context, track);
  }
}

/* 根据当前后端分派“停止循环播放”命令。 */
static void fighter_audio_stop_loop(fighter_audio_context_t *context) {
  if (!context) {
    return;
  }

  if (context->backend == FIGHTER_AUDIO_BACKEND_MMIO) {
    fighter_audio_stop_loop_mmio(context);
  } else if (context->backend == FIGHTER_AUDIO_BACKEND_COMMAND) {
    fighter_audio_stop_loop_command(context);
  }
}

/* 根据当前后端分派“播放一次音效”命令。 */
static void fighter_audio_play_once(fighter_audio_context_t *context,
                                    fighter_audio_track_t track) {
  if (!context) {
    return;
  }

  if (context->backend == FIGHTER_AUDIO_BACKEND_MMIO) {
    fighter_audio_play_once_mmio(context, track);
  } else if (context->backend == FIGHTER_AUDIO_BACKEND_COMMAND) {
    fighter_audio_play_once_command(context, track);
  }
}

/* 清空本帧音频命令队列。 */
void fighter_audio_command_list_clear(fighter_audio_command_list_t *list) {
  if (!list) {
    return;
  }

  memset(list, 0, sizeof(*list));
}

/* 向音频命令队列追加命令，队列满或参数无效时返回错误。 */
int fighter_audio_command_list_push(fighter_audio_command_list_t *list,
                                    fighter_audio_command_type_t type,
                                    fighter_audio_track_t track) {
  if (!list || list->count >= FIGHTER_AUDIO_MAX_COMMANDS) {
    return -1;
  }

  list->commands[list->count].type = type;
  list->commands[list->count].track = track;
  list->count++;
  return 0;
}

/* 返回音频 track 的可读名称。 */
const char *fighter_audio_track_name(fighter_audio_track_t track) {
  switch (track) {
    case FIGHTER_AUDIO_TRACK_MENU_BGM:
      return "menu_bgm";
    case FIGHTER_AUDIO_TRACK_MENU_CONFIRM:
      return "menu_confirm";
    case FIGHTER_AUDIO_TRACK_GAME_OVER:
      return "game_over";
    default:
      return "none";
  }
}

/* 返回音频 track 对应的资源文件路径。 */
const char *fighter_audio_track_path(fighter_audio_track_t track) {
  switch (track) {
    case FIGHTER_AUDIO_TRACK_MENU_BGM:
      return "../game_assets/sound effects/Title.wav";
    case FIGHTER_AUDIO_TRACK_MENU_CONFIRM:
      return "../game_assets/sound effects/Credit.wav";
    case FIGHTER_AUDIO_TRACK_GAME_OVER:
      return "../game_assets/sound effects/Game Over.wav";
    default:
      return NULL;
  }
}

/* 返回当前音频后端名称。 */
const char *fighter_audio_backend_name(const fighter_audio_context_t *context) {
  static char description[128];

  if (!context || context->backend == FIGHTER_AUDIO_BACKEND_DISABLED) {
    if (context && context->status_detail[0] != '\0') {
      snprintf(description, sizeof(description), "disabled (%s)",
               context->status_detail);
      return description;
    }
    return "disabled";
  }

  switch (context->backend) {
    case FIGHTER_AUDIO_BACKEND_MMIO:
      snprintf(description, sizeof(description), "wm8731-mmio (0x%08lX)",
               context->mmio_addr != 0 ? context->mmio_addr
                                       : (unsigned long)k_fighter_audio_default_mmio_addr);
      return description;
    case FIGHTER_AUDIO_BACKEND_COMMAND:
      switch (context->player_kind) {
        case FIGHTER_PLAYER_KIND_APLAY:
          if (context->aplay_device[0] != '\0') {
            snprintf(description, sizeof(description), "aplay (%s)",
                     context->aplay_device);
            return description;
          }
          return "aplay";
        case FIGHTER_PLAYER_KIND_FFPLAY:
          return "ffplay";
        case FIGHTER_PLAYER_KIND_AFPLAY:
          return "afplay";
        case FIGHTER_PLAYER_KIND_NONE:
        default:
          return "command";
      }
    case FIGHTER_AUDIO_BACKEND_DISABLED:
    default:
      return "disabled";
  }
}

/* 初始化音频选项，默认允许 command 播放后端。 */
void fighter_audio_options_init(fighter_audio_options_t *options) {
  if (!options) {
    return;
  }

  memset(options, 0, sizeof(*options));
}

/* 初始化音频系统，优先尝试 MMIO，必要时回退到 command/disabled 后端。 */
int fighter_audio_init(fighter_audio_context_t *context,
                       const fighter_audio_options_t *options) {
  int enable_command_audio;

  if (!context) {
    return -1;
  }

  memset(context, 0, sizeof(*context));
  context->backend = FIGHTER_AUDIO_BACKEND_DISABLED;
  context->mmio_addr = (unsigned long)k_fighter_audio_default_mmio_addr;
  context->bridge_reset_addr =
      (unsigned long)k_fighter_audio_default_bridge_reset_addr;

  enable_command_audio = options && options->enable_command_audio;
  if (!enable_command_audio) {
    return 0;
  }

  if (fighter_audio_mmio_init(context) == 0) {
    return 0;
  }

  if (fighter_audio_prepare_command_backend(context) == 0 &&
      context->player_kind != FIGHTER_PLAYER_KIND_NONE) {
    context->backend = FIGHTER_AUDIO_BACKEND_COMMAND;
    context->status_detail[0] = '\0';
  } else {
    if (context->status_detail[0] != '\0') {
      fprintf(stderr, "audio disabled: %s\n", context->status_detail);
    } else {
      fighter_audio_set_status_detail(
          context, "no usable WM8731/MMIO or command backend found");
      fprintf(stderr, "audio disabled: %s\n", context->status_detail);
    }
  }

  return 0;
}

/* 关闭音频系统，停止播放并释放后端资源。 */
void fighter_audio_close(fighter_audio_context_t *context) {
  if (!context) {
    return;
  }

  if (context->backend == FIGHTER_AUDIO_BACKEND_MMIO &&
      context->backend_data) {
    fighter_audio_mmio_destroy(
        (fighter_audio_mmio_state_t *)context->backend_data);
    context->backend_data = NULL;
  } else {
    fighter_audio_stop_loop_command(context);
    fighter_audio_reap_children();
  }

  context->backend = FIGHTER_AUDIO_BACKEND_DISABLED;
  context->looping_track = FIGHTER_AUDIO_TRACK_NONE;
}

/* 消费一帧内的音频命令队列，并分派到实际后端。 */
void fighter_audio_process_commands(fighter_audio_context_t *context,
                                    const fighter_audio_command_list_t *commands) {
  size_t i;

  fighter_audio_reap_children();

  if (!context || !commands) {
    return;
  }

  if (context->backend == FIGHTER_AUDIO_BACKEND_DISABLED &&
      !context->backend_logged) {
    context->backend_logged = 1;
  }

  for (i = 0; i < commands->count; ++i) {
    const fighter_audio_command_t *command = &commands->commands[i];
    switch (command->type) {
      case FIGHTER_AUDIO_COMMAND_PLAY_ONCE:
        fighter_audio_play_once(context, command->track);
        break;
      case FIGHTER_AUDIO_COMMAND_START_LOOP:
        fighter_audio_start_loop(context, command->track);
        break;
      case FIGHTER_AUDIO_COMMAND_STOP_LOOP:
        fighter_audio_stop_loop(context);
        break;
      case FIGHTER_AUDIO_COMMAND_NONE:
      default:
        break;
    }
  }
}
