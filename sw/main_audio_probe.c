#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * 音频 MMIO 探测程序。
 *
 * 这个小工具不走完整游戏音频线程，只负责打开 /dev/mem、使能 HPS-FPGA
 * bridge、读取 WM8731 音频 IP 的 4 个 32-bit 寄存器，确认硬件是否响应。
 */

enum {
  /* 音频 IP 暴露 4 个 32-bit word：control/fifospace/leftdata/rightdata。 */
  FIGHTER_AUDIO_MMIO_REG_COUNT = 4
};

static const off_t k_default_bridge_reset_addr = (off_t)0xFFD0501C;
static const off_t k_default_mmio_addr = (off_t)0xFF200000;

static int fighter_audio_probe_parse_address(const char *text,
                                             off_t default_value,
                                             off_t *value_out) {
  char *end;
  unsigned long long value;

  if (!value_out) {
    return -1;
  }

  if (!text || text[0] == '\0') {
    *value_out = default_value;
    return 0;
  }

  errno = 0;
  value = strtoull(text, &end, 0);
  if (errno != 0 || end == text || !end || *end != '\0') {
    return -1;
  }

  *value_out = (off_t)value;
  return 0;
}

static int fighter_audio_probe_parse_env_or_default(const char *env_name,
                                                    off_t default_value,
                                                    off_t *value_out) {
  const char *text;

  text = getenv(env_name);
  return fighter_audio_probe_parse_address(text, default_value, value_out);
}

static int fighter_audio_probe_map_region(int mem_fd,
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

static void fighter_audio_probe_unmap_region(void **map_base,
                                             size_t *map_length) {
  if (!map_base || !map_length || !*map_base || *map_length == 0) {
    return;
  }

  munmap(*map_base, *map_length);
  *map_base = NULL;
  *map_length = 0;
}

static void fighter_audio_probe_print_usage(const char *argv0) {
  printf("usage: %s [--mmio-addr HEX] [--bridge-addr HEX] [--no-enable-bridge]\n",
         argv0);
}

int main(int argc, char **argv) {
  off_t mmio_addr;
  off_t bridge_addr;
  int enable_bridge = 1;
  int mem_fd = -1;
  void *bridge_map = NULL;
  size_t bridge_map_length = 0;
  volatile uint32_t *bridge_reg = NULL;
  void *audio_map = NULL;
  size_t audio_map_length = 0;
  volatile uint32_t *audio_regs = NULL;
  uint32_t bridge_before = 0;
  uint32_t bridge_after = 0;
  uint32_t fifospace;
  int i;

  if (fighter_audio_probe_parse_env_or_default("FIGHTER_AUDIO_MMIO_ADDR",
                                               k_default_mmio_addr,
                                               &mmio_addr) != 0) {
    fprintf(stderr, "invalid FIGHTER_AUDIO_MMIO_ADDR\n");
    return 1;
  }
  if (fighter_audio_probe_parse_env_or_default("FIGHTER_AUDIO_BRIDGE_RESET_ADDR",
                                               k_default_bridge_reset_addr,
                                               &bridge_addr) != 0) {
    fprintf(stderr, "invalid FIGHTER_AUDIO_BRIDGE_RESET_ADDR\n");
    return 1;
  }

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--mmio-addr") == 0 && i + 1 < argc) {
      ++i;
      if (fighter_audio_probe_parse_address(argv[i], mmio_addr, &mmio_addr) != 0) {
        fprintf(stderr, "invalid --mmio-addr: %s\n", argv[i]);
        return 1;
      }
    } else if (strcmp(argv[i], "--bridge-addr") == 0 && i + 1 < argc) {
      ++i;
      if (fighter_audio_probe_parse_address(argv[i], bridge_addr, &bridge_addr) !=
          0) {
        fprintf(stderr, "invalid --bridge-addr: %s\n", argv[i]);
        return 1;
      }
    } else if (strcmp(argv[i], "--no-enable-bridge") == 0) {
      enable_bridge = 0;
    } else if (strcmp(argv[i], "--help") == 0) {
      fighter_audio_probe_print_usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "unknown argument: %s\n", argv[i]);
      fighter_audio_probe_print_usage(argv[0]);
      return 1;
    }
  }

  mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (mem_fd < 0) {
    fprintf(stderr, "open /dev/mem failed: %s\n", strerror(errno));
    return 1;
  }

  if (fighter_audio_probe_map_region(mem_fd, bridge_addr, sizeof(uint32_t),
                                     &bridge_map, &bridge_map_length,
                                     &bridge_reg) != 0) {
    fprintf(stderr, "map bridge reset 0x%08lX failed: %s\n",
            (unsigned long)bridge_addr, strerror(errno));
    close(mem_fd);
    return 1;
  }

  if (fighter_audio_probe_map_region(mem_fd, mmio_addr,
                                     FIGHTER_AUDIO_MMIO_REG_COUNT *
                                         sizeof(uint32_t),
                                     &audio_map, &audio_map_length,
                                     &audio_regs) != 0) {
    fprintf(stderr, "map audio mmio 0x%08lX failed: %s\n",
            (unsigned long)mmio_addr, strerror(errno));
    fighter_audio_probe_unmap_region(&bridge_map, &bridge_map_length);
    close(mem_fd);
    return 1;
  }

  bridge_before = *bridge_reg;
  bridge_after = bridge_before;
  if (enable_bridge) {
    bridge_after &= ~0x3U;
    *bridge_reg = bridge_after;
    bridge_after = *bridge_reg;
  }

  printf("bridge_reset_addr : 0x%08lX\n", (unsigned long)bridge_addr);
  printf("audio_mmio_addr   : 0x%08lX\n", (unsigned long)mmio_addr);
  printf("bridge_before     : 0x%08" PRIX32 "\n", bridge_before);
  printf("bridge_after      : 0x%08" PRIX32 "\n", bridge_after);

  for (i = 0; i < FIGHTER_AUDIO_MMIO_REG_COUNT; ++i) {
    printf("reg[%d]            : 0x%08" PRIX32 "\n", i, audio_regs[i]);
  }

  fifospace = audio_regs[1];
  printf("left_write_space  : %" PRIu32 "\n", (fifospace >> 24) & 0xFFU);
  printf("right_write_space : %" PRIu32 "\n", (fifospace >> 16) & 0xFFU);

  fighter_audio_probe_unmap_region(&audio_map, &audio_map_length);
  fighter_audio_probe_unmap_region(&bridge_map, &bridge_map_length);
  close(mem_fd);
  return 0;
}
