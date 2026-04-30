# Software Module Notes

This directory contains the HPS-side game code, input handling, animation
binding, rendering backends, and smoke tests for the DE1-SoC fighting game.

## Default Assumption

- Target board: DE1-SoC.
- Input: up to two USB HID keyboards through the HPS USB host.
- Player 1 uses keyboard 1; player 2 uses keyboard 2.
- Both players use the same key layout.

## Default Mapping

- `W`: jump
- `A`: move left
- `D`: move right
- `S`: crouch
- `J`: normal attack
- `A + J`: fireball
- `D + J`: dragon punch
- `W + J`: jump attack
- `W + D + J`: forward jump attack
- `W + A + J`: back jump attack
- `S + J`: sweep
- `K`: guard
- `L`: exit the current round and return to the menu

Menu input is read from keyboard 1:

- `A / D`: change menu selection
- `J`: confirm

## Files

- `main_phase1_demo.c`: scripted or USB-driven integrated demo.
- `game/`: game state machine, combat rules, health, timer, projectiles.
- `input/`: HID report parsing and gameplay command mapping.
- `render_if/`: console, Linux framebuffer, and FPGA VGA MMIO render backends.
- `fighter_animation.c`: sprite animation loading and frame selection.
- `tests/test_phase1.c`: focused gameplay and rendering-interface tests.
- `main_vga_probe.c`: minimal VGA MMIO bring-up tool.

## Build

On the HPS Linux target, install the usual C build tools. USB support also needs
`libusb-1.0` development files:

```bash
apt install -y gcc make pkg-config libusb-1.0-0-dev
```

Build everything available for the current system:

```bash
cd sw
make
```

Core executables:

```bash
./phase1_demo
./phase1_test
./vga_probe
```

If `libusb-1.0` is available, `make` also builds:

```bash
./input_demo
```

## Running

The integrated demo can run without USB using deterministic scripts:

```bash
./phase1_demo --script smoke --console
./phase1_demo --script ko --console
```

Use the hardware renderer when the VGA IP is programmed:

```bash
./phase1_demo --script smoke
./phase1_demo --usb
```

The renderer first tries FPGA VGA MMIO at `0xFF240000`. If unavailable, it falls
back to Linux framebuffer and then console output.

Game assets are loaded from `/root/game_assets` by default, with a local fallback
to `../game_assets` when running from `sw/`. Override the path when needed:

```bash
FIGHTER_ASSET_ROOT=/path/to/game_assets ./phase1_demo
```

VGA bring-up can be checked independently:

```bash
./vga_probe
```

It probes the `VPGA` identifier and writes a simple RGB565 gradient to confirm
the HPS-to-FPGA framebuffer path.
