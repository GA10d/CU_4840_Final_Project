#ifndef FIGHTER_VGA_MMIO_H
#define FIGHTER_VGA_MMIO_H

/*
 * VGA 自定义 IP 的 Avalon-MM 寄存器定义。
 *
 * 所有寄存器都是 32-bit word，offset 也按 word 计数：
 * 这样 HPS 端可以直接用 volatile uint32_t *regs 访问 regs[offset]。
 * 使用 32 bit 的原因不是每个字段都需要 32 bit，而是轻量级 HPS-FPGA
 * bridge 和硬件 IP 的数据总线宽度就是 32 bit；word 对齐能避免半字访问
 * 和字节序带来的麻烦。
 */
enum {
  /* CONTROL: bit0 present，bit1 swap request/pending，bit8/9 显示/写入 buffer。 */
  FIGHTER_VGA_MMIO_REG_CONTROL = 0,
  /* WIDTH/HEIGHT/STRIDE 都返回 32-bit 数字，便于软件校验 framebuffer 几何。 */
  FIGHTER_VGA_MMIO_REG_WIDTH = 1,
  FIGHTER_VGA_MMIO_REG_HEIGHT = 2,
  FIGHTER_VGA_MMIO_REG_STRIDE = 3,
  /* IDENT: 0x56504741，即 ASCII "VPGA"，用于探测映射地址是否正确。 */
  FIGHTER_VGA_MMIO_REG_IDENT = 31,
  /* framebuffer 从 1024 word 起，前 4KB 留给控制寄存器/未来扩展。 */
  FIGHTER_VGA_MMIO_REG_FRAME_WORD_OFFSET = 1024,

  /* 320x240 RGB565；两个 16-bit 像素打包进一个 32-bit word。 */
  FIGHTER_VGA_MMIO_FRAME_WIDTH = 320,
  FIGHTER_VGA_MMIO_FRAME_HEIGHT = 240,
  FIGHTER_VGA_MMIO_FRAME_WORD_COUNT =
      (FIGHTER_VGA_MMIO_FRAME_WIDTH * FIGHTER_VGA_MMIO_FRAME_HEIGHT) / 2,
  FIGHTER_VGA_MMIO_REG_SPAN_COUNT =
      FIGHTER_VGA_MMIO_REG_FRAME_WORD_OFFSET +
      FIGHTER_VGA_MMIO_FRAME_WORD_COUNT
};

#define FIGHTER_VGA_MMIO_CONTROL_PRESENT (1U << 0)
#define FIGHTER_VGA_MMIO_CONTROL_SWAP_REQUEST (1U << 1)
#define FIGHTER_VGA_MMIO_CONTROL_SWAP_PENDING (1U << 1)
#define FIGHTER_VGA_MMIO_CONTROL_DISPLAY_BUFFER (1U << 8)
#define FIGHTER_VGA_MMIO_CONTROL_WRITE_BUFFER (1U << 9)

#endif
