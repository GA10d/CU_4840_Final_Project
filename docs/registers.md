# Register Notes

This document records the current memory-mapped interfaces after the project was
reduced to the gameplay, input, animation, and VGA display path.

## Address Map

| Block | Qsys offset | Linux physical address | Purpose |
| --- | ---: | ---: | --- |
| `fighter_vga_0` | `0x40000` | `0xFF240000` | VGA framebuffer renderer |

The lightweight HPS-to-FPGA bridge base is `0xFF200000`. The VGA renderer keeps
its `0x40000` Platform Designer offset, so software maps it at `0xFF240000`.

## Bridge Enable

Software enables the HPS-to-FPGA bridges by mapping the bridge reset register at
`0xFFD0501C`, clearing bits `[1:0]`, and writing the value back.

The helper for this path lives in:

- `sw/render_if/fighter_renderer.c`

## VGA Hardware Registers

The VGA hardware source is:

- `hw/fighter_vga_renderer.sv`
- `hw/fighter_vga_hw.tcl`

The renderer exposes a small control/status area and a 320 by 240 RGB565
framebuffer window. Software writes full frames through MMIO, then requests a
buffer swap so the displayed frame changes during vertical blanking.

Important software-facing constants are defined in:

- `sw/include/fighter_renderer.h`
- `sw/render_if/fighter_renderer.c`

## VGA Software Tools

The standalone bring-up tool is:

- `sw/main_vga_probe.c`

It maps the VGA MMIO region, verifies the `VPGA` identifier, writes a test
gradient, and asks the renderer to present it.

## Game State Encoding

The gameplay state encoder is separate from the VGA framebuffer path:

- `sw/include/fighter_mmio.h`
- `sw/render_if/fighter_mmio.c`

It packs game state, player state, health, timer, event flags, projectile state,
and animation hints into a compact register-style array for renderer-side
inspection and tests.

## Generated Hardware Artifacts

Platform Designer output under `hw/soc_system/` is generated from:

- `hw/soc_system.qsys`
- `hw/soc_system.tcl`
- `hw/soc_system_top.sv`

Regenerate it with:

```bash
cd hw
make qsys
```
