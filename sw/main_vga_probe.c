#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum {
  FIGHTER_VGA_REG_GAME_STATE = 0,
  FIGHTER_VGA_REG_PLAYER1_X = 1,
  FIGHTER_VGA_REG_PLAYER1_Y = 2,
  FIGHTER_VGA_REG_PLAYER1_STATE = 3,
  FIGHTER_VGA_REG_PLAYER2_X = 4,
  FIGHTER_VGA_REG_PLAYER2_Y = 5,
  FIGHTER_VGA_REG_PLAYER2_STATE = 6,
  FIGHTER_VGA_REG_PLAYER1_HP = 7,
  FIGHTER_VGA_REG_PLAYER2_HP = 8,
  FIGHTER_VGA_REG_ROUND_TIMER = 9,
  FIGHTER_VGA_REG_WINNER = 10,
  FIGHTER_VGA_REG_PLAYER1_FACING = 11,
  FIGHTER_VGA_REG_PLAYER2_FACING = 12,
  FIGHTER_VGA_REG_IDENT = 31,
  FIGHTER_VGA_REG_SPAN_COUNT = 32
};

static const off_t k_default_bridge_reset_addr = (off_t)0xFFD0501C;
static const off_t k_default_vga_mmio_addr = (off_t)0xFF200080;
static const uint32_t k_vga_ident = 0x56504741U;

static int parse_address(const char *text, off_t default_value, off_t *out) {
  char *end;
  unsigned long long value;

  if (!out) {
    return -1;
  }
  if (!text || text[0] == '\0') {
    *out = default_value;
    return 0;
  }

  errno = 0;
  value = strtoull(text, &end, 0);
  if (errno != 0 || end == text || !end || *end != '\0') {
    return -1;
  }

  *out = (off_t)value;
  return 0;
}

static int map_physical(int mem_fd,
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

  page_size = sysconf(_SC_PAGESIZE);
  if (mem_fd < 0 || page_size <= 0 || !map_base || !map_length ||
      !register_base || span == 0) {
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

static void unmap_region(void **map_base, size_t *map_length) {
  if (!map_base || !map_length || !*map_base || *map_length == 0) {
    return;
  }

  munmap(*map_base, *map_length);
  *map_base = NULL;
  *map_length = 0;
}

static void print_usage(const char *argv0) {
  printf("usage: %s [--mmio-addr HEX] [--bridge-addr HEX] [--no-write-test] "
         "[--scan]\n",
         argv0);
}

int main(int argc, char **argv) {
  off_t mmio_addr = k_default_vga_mmio_addr;
  off_t bridge_addr = k_default_bridge_reset_addr;
  int write_test = 1;
  int scan = 0;
  int mmio_addr_overridden = 0;
  int mem_fd;
  void *bridge_map = NULL;
  void *vga_map = NULL;
  size_t bridge_map_length = 0;
  size_t vga_map_length = 0;
  volatile uint32_t *bridge_reg = NULL;
  volatile uint32_t *vga_regs = NULL;
  uint32_t bridge_before;
  uint32_t bridge_after;
  uint32_t ident;
  int i;

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--mmio-addr") == 0 && i + 1 < argc) {
      if (parse_address(argv[++i], mmio_addr, &mmio_addr) != 0) {
        fprintf(stderr, "invalid --mmio-addr\n");
        return 1;
      }
      mmio_addr_overridden = 1;
    } else if (strcmp(argv[i], "--bridge-addr") == 0 && i + 1 < argc) {
      if (parse_address(argv[++i], bridge_addr, &bridge_addr) != 0) {
        fprintf(stderr, "invalid --bridge-addr\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--no-write-test") == 0) {
      write_test = 0;
    } else if (strcmp(argv[i], "--scan") == 0) {
      scan = 1;
      write_test = 0;
    } else if (strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "unknown argument: %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  if (scan && !mmio_addr_overridden) {
    mmio_addr = (off_t)0xFF200000;
  }

  mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (mem_fd < 0) {
    fprintf(stderr, "open /dev/mem failed: %s\n", strerror(errno));
    return 1;
  }

  if (map_physical(mem_fd, bridge_addr, sizeof(uint32_t), &bridge_map,
                   &bridge_map_length, &bridge_reg) != 0) {
    fprintf(stderr, "map bridge reset 0x%08lX failed: %s\n",
            (unsigned long)bridge_addr, strerror(errno));
    close(mem_fd);
    return 1;
  }

  if (map_physical(mem_fd, mmio_addr,
                   FIGHTER_VGA_REG_SPAN_COUNT * sizeof(uint32_t), &vga_map,
                   &vga_map_length, &vga_regs) != 0) {
    fprintf(stderr, "map VGA MMIO 0x%08lX failed: %s\n",
            (unsigned long)mmio_addr, strerror(errno));
    unmap_region(&bridge_map, &bridge_map_length);
    close(mem_fd);
    return 1;
  }

  bridge_before = *bridge_reg;
  *bridge_reg = bridge_before & ~0x3U;
  bridge_after = *bridge_reg;
  ident = vga_regs[FIGHTER_VGA_REG_IDENT];

  printf("bridge_reset_addr : 0x%08lX\n", (unsigned long)bridge_addr);
  printf("vga_mmio_addr     : 0x%08lX\n", (unsigned long)mmio_addr);
  printf("bridge_before     : 0x%08" PRIX32 "\n", bridge_before);
  printf("bridge_after      : 0x%08" PRIX32 "\n", bridge_after);
  printf("ident             : 0x%08" PRIX32 " %s\n", ident,
         ident == k_vga_ident ? "(VPGA OK)" : "(unexpected)");

  if (scan) {
    uint32_t word;

    printf("scan              : reading 0x%08lX..0x%08lX\n",
           (unsigned long)mmio_addr, (unsigned long)mmio_addr + 0x0fffUL);
    for (word = 0; word < 0x1000U / sizeof(uint32_t); ++word) {
      uint32_t value = vga_regs[word];
      if (value == k_vga_ident) {
        printf("found VPGA        : 0x%08" PRIX32 "\n",
               (uint32_t)mmio_addr + word * 4U);
      } else if (word < 64U) {
        printf("word[0x%03" PRIX32 "]      : 0x%08" PRIX32 "\n",
               word * 4U, value);
      }
    }
  }

  if (write_test) {
    vga_regs[FIGHTER_VGA_REG_GAME_STATE] = 1U;
    vga_regs[FIGHTER_VGA_REG_PLAYER1_X] = 96U;
    vga_regs[FIGHTER_VGA_REG_PLAYER1_Y] = 304U;
    vga_regs[FIGHTER_VGA_REG_PLAYER1_STATE] = 5U;
    vga_regs[FIGHTER_VGA_REG_PLAYER2_X] = 464U;
    vga_regs[FIGHTER_VGA_REG_PLAYER2_Y] = 304U;
    vga_regs[FIGHTER_VGA_REG_PLAYER2_STATE] = 4U;
    vga_regs[FIGHTER_VGA_REG_PLAYER1_HP] = 80U;
    vga_regs[FIGHTER_VGA_REG_PLAYER2_HP] = 35U;
    vga_regs[FIGHTER_VGA_REG_ROUND_TIMER] = 77U;
    vga_regs[FIGHTER_VGA_REG_WINNER] = 0U;
    vga_regs[FIGHTER_VGA_REG_PLAYER1_FACING] = 1U;
    vga_regs[FIGHTER_VGA_REG_PLAYER2_FACING] = 0U;
    printf("write_test        : wrote fixed PLAYING scene registers\n");
  }

  unmap_region(&vga_map, &vga_map_length);
  unmap_region(&bridge_map, &bridge_map_length);
  close(mem_fd);
  return ident == k_vga_ident ? 0 : 2;
}
