# Project Register Documentation

This document describes the register interfaces that are active in the
current project. The old game-state MMIO encoder path has been removed; the
display hardware now consumes a packed RGB565 framebuffer, not player/game
state words.

## Address Map

`hw/soc_system.qsys` connects the custom FPGA peripherals to the HPS
lightweight HPS-to-FPGA bridge. The bridge's Linux physical base is
`0xFF200000`, and each Platform Designer base address is added to that base.

| Module | Qsys base offset | Linux physical address | Purpose |
|---|---:|---:|---|
| `fighter_audio_0` | `0x0000` | `0xFF200000` | WM8731 audio MMIO IP |
| `fighter_vga_0` | `0x40000` | `0xFF240000` | VGA framebuffer MMIO IP |

Software also maps the bridge reset register:

| Address | Name | Purpose |
|---:|---|---|
| `0xFFD0501C` | bridge reset register | Clear bits `[1:0]` to enable the HPS-FPGA bridges |

## VGA Hardware Registers

Source of truth:

- `hw/fighter_vga_renderer.sv`
- `hw/soc_system/synthesis/submodules/fighter_vga_renderer.sv`
- `sw/include/fighter_vga_mmio.h`

The VGA IP is an Avalon-MM slave. Its address units are words, and its data
bus is 32 bits wide. A software access such as `regs[31]` means word offset
`31`, or byte offset `31 * 4`.

### HPS-Visible Registers

| Word offset | Name | Access | Meaning |
|---:|---|---|---|
| `0` | `REG_CONTROL` | R/W | Control/status |
| `1` | `REG_WIDTH` | R | `320` |
| `2` | `REG_HEIGHT` | R | `240` |
| `3` | `REG_STRIDE` | R | `640` bytes per framebuffer row |
| `31` | `REG_IDENT` | R | `0x56504741`, ASCII `"VPGA"` |
| `1024..39423` | framebuffer words | W | Packed RGB565 pixels, two pixels per 32-bit word |

`REG_CONTROL` read bits:

| Bit | Name | Meaning |
|---:|---|---|
| `0` | present | VGA IP is present |
| `1` | swap pending | A requested buffer swap is waiting for vblank |
| `8` | display buffer | The framebuffer bank currently scanned by VGA |
| `9` | write buffer | The framebuffer bank currently written by HPS |

`REG_CONTROL` write bits:

| Bit | Name | Meaning |
|---:|---|---|
| `1` | swap request | Request hardware to swap buffers at the next vblank |

### Framebuffer Layout

| Parameter | Value |
|---|---:|
| framebuffer width | `320` pixels |
| framebuffer height | `240` pixels |
| framebuffer words per row | `160` |
| framebuffer word count | `38400` |
| framebuffer word offset | `1024` |
| last valid word offset | `39423` |
| mapped word count needed by software | `39424` |
| mapped byte count needed by software | `157696` |

Each 32-bit framebuffer word stores two RGB565 pixels:

| Bits | Meaning |
|---|---|
| `[15:0]` | first RGB565 pixel |
| `[31:16]` | second RGB565 pixel |

RGB565 layout:

| Bits | Channel |
|---|---|
| `[15:11]` | red, 5 bits |
| `[10:5]` | green, 6 bits |
| `[4:0]` | blue, 5 bits |

The VGA output timing is 640 x 480. The hardware scales the 320 x 240
framebuffer by repeating each source pixel as a 2 x 2 block:

- `source_x = h_count[9:1]`
- `source_y = v_count[8:1]`

### Double Buffering

The VGA IP has two framebuffer banks.

1. The HPS writes the non-displayed bank.
2. After writing a full frame, software writes the swap request bit to
   `REG_CONTROL`.
3. Hardware sets `swap_pending`.
4. At vblank, hardware toggles `display_buffer` and clears `swap_pending`.

This avoids tearing because the VGA scanout side never reads the bank being
updated by software.

## VGA Software Usage

`sw/render_if/fighter_renderer.c` is the active game renderer for the FPGA VGA
IP. It:

1. Enables the HPS-FPGA bridges through `0xFFD0501C`.
2. Maps the VGA IP at `0xFF240000` by default.
3. Verifies `REG_IDENT == 0x56504741`.
4. Verifies the geometry registers report `320`, `240`, and `640`.
5. Waits for the previous swap request to complete.
6. Writes `38400` packed RGB565 words starting at word offset `1024`.
7. Writes the swap request bit to `REG_CONTROL`.

`sw/main_vga_probe.c` is a standalone hardware probe. It uses the same
`sw/include/fighter_vga_mmio.h` register definitions, writes a test gradient
into the framebuffer, and requests a swap.

## Audio Hardware Registers

Source of truth:

- `hw/fighter_audio.sv`
- `sw/include/fighter_audio_mmio.h`
- `sw/audio/fighter_audio.c`

The audio IP is also a 32-bit Avalon-MM slave. It has four word registers:

| Word offset | Name | Access | Purpose |
|---:|---|---|---|
| `0` | control/status | R/W | Codec status, FIFO status, and status clear bits |
| `1` | fifospace | R | Remaining left/right FIFO write space |
| `2` | leftdata | W | Left channel sample word |
| `3` | rightdata | W | Right channel sample word |

Control/status read bits:

| Bits | Meaning |
|---|---|
| `[31:24]` | left FIFO count |
| `[23:16]` | right FIFO count |
| `3` | write overflow seen |
| `2` | transmit underflow seen |
| `1` | codec init error |
| `0` | codec init done |

Control/status write bits:

| Bit | Meaning |
|---:|---|
| `3` | clear write overflow/status path |
| `2` | clear transmit underflow/status path |

Software writes signed 16-bit PCM samples into the upper 16 bits of each
32-bit sample word because the serializer drives the WM8731 in a left-justified
16-bit format.

## Platform Designer Interface Notes

`hw/fighter_vga_hw.tcl` registers the VGA IP with these relevant properties:

| Property | Value |
|---|---|
| Avalon address units | `WORDS` |
| Avalon data width | `32` |
| Avalon address width | `16` |
| explicit address span | `262144` bytes |
| read wait time | `1` cycle |
| write wait time | `0` cycles |

The `0x40000` VGA offset is manually assigned in Platform Designer/Qsys. It is
not dynamically allocated by `make`.

## Practical VGA Sequence

1. Map `0xFFD0501C` and clear bits `[1:0]`.
2. Map `0xFF240000` for at least `39424 * 4` bytes.
3. Read word `31` and check for `0x56504741`.
4. Read words `1`, `2`, and `3`; expect `320`, `240`, and `640`.
5. Wait until word `0` bit `1` is clear.
6. Write `38400` packed RGB565 words starting at word `1024`.
7. Write `1 << 1` to word `0` to request a swap.

## Generated HPS Register Files

Files such as `hw/soc_system/synthesis/soc_system_hps_0_hps.svd` and
`hw/soc_system/synthesis/soc_system.regmap` describe the full Cyclone V HPS
register space. They are generated platform reference files, not the primary
definition of this project's custom VGA/audio IP register maps.
