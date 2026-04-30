# Project QA

## What Is The Project?

This is a two-player fighting game for the DE1-SoC platform. The HPS runs input,
game logic, sprite animation, and frame preparation. The FPGA fabric provides a
custom VGA framebuffer renderer.

## What Runs On The HPS?

The HPS software reads scripted or USB keyboard input, updates the game state at
about 60 Hz, resolves movement and attacks, chooses animation frames, and writes
RGB565 frames to the VGA MMIO region.

## What Runs On The FPGA?

The FPGA side contains the custom VGA renderer. It stores a 320 by 240 RGB565
framebuffer, scales timing for 640 by 480 VGA output, and swaps buffers during
vertical blanking.

## Why Split The System This Way?

Game rules are branchy and easier to develop in C on the HPS. VGA signaling
needs deterministic timing and direct board pin control, so it belongs in the
FPGA fabric.

## How Does MMIO Work?

The HPS opens `/dev/mem`, maps the physical address of the custom IP, and reads
or writes through a `volatile uint32_t *`. The VGA IP is mapped at `0xFF240000`.
Before using it, software enables the HPS-to-FPGA bridge by clearing bits `[1:0]`
of the bridge reset register at `0xFFD0501C`.

## What Is The VGA Register Contract?

The VGA block exposes identification and geometry registers plus a framebuffer
window. Software writes RGB565 pixels into the inactive buffer and then requests
a swap. Hardware presents the new buffer at the vertical blank point.

## How Is Gameplay Tested?

`sw/tests/test_phase1.c` covers menu transitions, round start and exit, timeout,
knockout, hit and block resolution, projectile behavior, jump restrictions,
animation hold behavior, and game-state encoding.

## How Is VGA Tested?

`sw/main_vga_probe.c` maps the VGA register block, checks the `VPGA` identifier,
and writes a simple gradient frame. The integrated demos exercise the same
renderer through scripted or USB-controlled gameplay.

## What Should Be Demoed?

Use this order:

1. `phase1_test` for game logic.
2. `vga_probe` for the hardware framebuffer path.
3. `phase1_demo --script smoke` or `phase1_demo --script ko` for the integrated
   flow.
4. `phase1_demo --usb` when the board has keyboards attached and `libusb` is
   available.

## What Is The Main Engineering Point?

The project shows a clean HPS-to-FPGA game pipeline: software game engine,
custom VGA framebuffer output, keyboard input, deterministic tests, and a narrow
MMIO contract between software and hardware.
