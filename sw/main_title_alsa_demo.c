#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const off_t k_default_bridge_reset_addr = (off_t)0xFFD0501C;

static int title_demo_copy_string(const char *src, char *dst, size_t dst_size) {
  size_t length;

  if (!src || !dst || dst_size == 0) {
    return -1;
  }

  length = strlen(src);
  if (length + 1 > dst_size) {
    return -1;
  }

  memcpy(dst, src, length + 1);
  return 0;
}

static int title_demo_parse_address(const char *text, off_t default_value,
                                    off_t *value_out) {
  char *end;
  unsigned long long parsed;

  if (!value_out) {
    return -1;
  }

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

static int title_demo_map_register(int mem_fd, off_t physical_addr,
                                   size_t span, void **map_base,
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

static void title_demo_unmap_register(void **map_base, size_t *map_length) {
  if (!map_base || !map_length || !*map_base || *map_length == 0) {
    return;
  }

  munmap(*map_base, *map_length);
  *map_base = NULL;
  *map_length = 0;
}

static int title_demo_enable_h2f_bridges(off_t bridge_addr,
                                         uint32_t *before_value,
                                         uint32_t *after_value) {
  int mem_fd = -1;
  void *map_base = NULL;
  size_t map_length = 0;
  volatile uint32_t *bridge_reg = NULL;
  uint32_t before = 0;
  uint32_t after = 0;

  if (!before_value || !after_value) {
    return -1;
  }

  mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (mem_fd < 0) {
    return -1;
  }

  if (title_demo_map_register(mem_fd, bridge_addr, sizeof(uint32_t), &map_base,
                              &map_length, &bridge_reg) != 0) {
    close(mem_fd);
    return -1;
  }

  before = *bridge_reg;
  after = before & ~0x3U;
  *bridge_reg = after;
  after = *bridge_reg;

  *before_value = before;
  *after_value = after;

  title_demo_unmap_register(&map_base, &map_length);
  close(mem_fd);
  return 0;
}

static int title_demo_resolve_asset(const char *override_path, char *buffer,
                                    size_t buffer_size) {
  static const char *const k_candidates[] = {
      "../game_assets/sound effects/Title.wav",
      "game_assets/sound effects/Title.wav",
      NULL};
  size_t i;

  if (!buffer || buffer_size == 0) {
    return -1;
  }

  if (override_path && override_path[0] != '\0') {
    if (access(override_path, R_OK) != 0) {
      return -1;
    }
    return title_demo_copy_string(override_path, buffer, buffer_size);
  }

  for (i = 0; k_candidates[i] != NULL; ++i) {
    if (access(k_candidates[i], R_OK) == 0) {
      return title_demo_copy_string(k_candidates[i], buffer, buffer_size);
    }
  }

  return -1;
}

static int title_demo_run_aplay_list(void) {
  pid_t pid;
  int status;

  pid = fork();
  if (pid < 0) {
    return -1;
  }

  if (pid == 0) {
    execlp("aplay", "aplay", "-l", (char *)NULL);
    _exit(127);
  }

  if (waitpid(pid, &status, 0) < 0) {
    return -1;
  }

  if (!WIFEXITED(status)) {
    return -1;
  }

  return WEXITSTATUS(status);
}

static int title_demo_detect_device(char *buffer, size_t buffer_size) {
  const char *override;
  FILE *stream;
  char line[256];
  int card;
  int device;

  if (!buffer || buffer_size == 0) {
    return -1;
  }

  override = getenv("FIGHTER_AUDIO_DEVICE");
  if (override && override[0] != '\0') {
    return title_demo_copy_string(override, buffer, buffer_size);
  }

  stream = popen("aplay -l 2>/dev/null", "r");
  if (!stream) {
    return -1;
  }

  while (fgets(line, sizeof(line), stream)) {
    if (sscanf(line, "card %d: %*[^,], device %d:", &card, &device) == 2 &&
        (strstr(line, "DE1SOC") != NULL || strstr(line, "WM8731") != NULL)) {
      snprintf(buffer, buffer_size, "plughw:%d,%d", card, device);
      pclose(stream);
      return 0;
    }
  }

  pclose(stream);
  return -1;
}

static void title_demo_print_usage(const char *argv0) {
  printf("usage: %s [--device NAME] [--asset PATH] [--bridge-addr HEX]\n",
         argv0);
  printf("       %s [--skip-bridge-reset] [--list-cards] [--dry-run]\n",
         argv0);
}

int main(int argc, char **argv) {
  char asset_path[512];
  char device_name[64];
  const char *device_override = NULL;
  const char *asset_override = NULL;
  off_t bridge_addr = k_default_bridge_reset_addr;
  int skip_bridge_reset = 0;
  int list_cards = 0;
  int dry_run = 0;
  int i;
  uint32_t bridge_before = 0;
  uint32_t bridge_after = 0;

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
      ++i;
      device_override = argv[i];
    } else if (strcmp(argv[i], "--asset") == 0 && i + 1 < argc) {
      ++i;
      asset_override = argv[i];
    } else if (strcmp(argv[i], "--bridge-addr") == 0 && i + 1 < argc) {
      ++i;
      if (title_demo_parse_address(argv[i], bridge_addr, &bridge_addr) != 0) {
        fprintf(stderr, "invalid --bridge-addr: %s\n", argv[i]);
        return 1;
      }
    } else if (strcmp(argv[i], "--skip-bridge-reset") == 0) {
      skip_bridge_reset = 1;
    } else if (strcmp(argv[i], "--list-cards") == 0) {
      list_cards = 1;
    } else if (strcmp(argv[i], "--dry-run") == 0) {
      dry_run = 1;
    } else if (strcmp(argv[i], "--help") == 0) {
      title_demo_print_usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "unknown argument: %s\n", argv[i]);
      title_demo_print_usage(argv[0]);
      return 1;
    }
  }

  if (list_cards) {
    return title_demo_run_aplay_list();
  }

  if (title_demo_resolve_asset(asset_override, asset_path,
                               sizeof(asset_path)) != 0) {
    fprintf(stderr,
            "failed to resolve Title.wav; use --asset to point at the file\n");
    return 1;
  }

  if (device_override && device_override[0] != '\0') {
    if (title_demo_copy_string(device_override, device_name,
                               sizeof(device_name)) != 0) {
      fprintf(stderr, "device name is too long\n");
      return 1;
    }
  } else if (title_demo_detect_device(device_name, sizeof(device_name)) != 0) {
    fprintf(stderr,
            "failed to auto-detect a DE1-SoC WM8731 ALSA device; run with "
            "--list-cards or pass --device\n");
    return 1;
  }

  if (!skip_bridge_reset) {
    if (title_demo_enable_h2f_bridges(bridge_addr, &bridge_before,
                                      &bridge_after) != 0) {
      fprintf(stderr,
              "failed to release HPS-to-FPGA bridges from reset via "
              "/dev/mem at 0x%08lX: %s\n",
              (unsigned long)bridge_addr, strerror(errno));
      return 1;
    }
  }

  printf("asset        : %s\n", asset_path);
  printf("alsa device  : %s\n", device_name);
  if (skip_bridge_reset) {
    printf("bridge reset : skipped\n");
  } else {
    printf("bridge addr  : 0x%08lX\n", (unsigned long)bridge_addr);
    printf("bridge before: 0x%08" PRIX32 "\n", bridge_before);
    printf("bridge after : 0x%08" PRIX32 "\n", bridge_after);
  }

  if (dry_run) {
    return 0;
  }

  execlp("aplay", "aplay", "-D", device_name, "-q", asset_path, (char *)NULL);

  fprintf(stderr, "failed to exec aplay: %s\n", strerror(errno));
  return 1;
}
