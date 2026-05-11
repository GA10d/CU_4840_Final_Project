75-Minute Q&A Script

Section 1 System Overview, About 8 Minutes

Q1. Show me your overall system. What is the project actually doing
A
Our project is a two-player fighting game implemented on the DE1-SoC platform. The system is split between the HPS and the FPGA fabric. The HPS runs the main C software it reads USB keyboard input, updates the game state, loads sprite and audio assets, renders each frame into a software framebuffer, and sends audio samples. The FPGA side contains two custom hardware IP blocks one VGA framebuffer renderer and one WM8731 audio controller. The HPS communicates with both IP blocks through memory-mapped IO over the lightweight HPS-to-FPGA bridge.

Q2. What parts are software and what parts are hardware
A
The software handles the game logic menu state, playing state, game-over state, player movement, attacks, collision, health, timer, projectiles, animation selection, and asset loading. The hardware handles time-critical IO. The VGA IP stores a 320 by 240 RGB565 framebuffer and generates 640 by 480 VGA timing. The audio IP initializes the WM8731 codec over I2C, accepts PCM samples through MMIO FIFOs, and serializes audio to the codec.

Q3. So is the FPGA computing the game logic
A
No, not in the current design. The FPGA is used as a custom display and audio peripheral. The game logic is computed on the HPS. The FPGA accelerates or implements the low-level real-time interfaces VGA signal generation and audio streaming. That separation made the system easier to debug because the HPS can produce complete frames and the FPGA only needs to display them reliably.

Q4. Why did you choose that partition
A
The game logic has many branches player states, attack phases, input edges, collision rules, animation selection, and asset management. Those are easier to develop and test in C on the HPS. VGA and audio, however, need stable timing and direct board pin control, so they are natural FPGA tasks. This partition also lets us test gameplay without hardware using the console renderer, then test VGA and audio separately with probe programs.

Section 2 HPS-to-FPGA Communication, About 10 Minutes

Q5. How does the HPS actually send data to your hardware
A
The HPS software opens /dev/mem, maps the physical MMIO address of the custom IP, and then reads or writes through a volatile uint32_t pointer. For VGA, the default physical address is 0xFF240000. For audio, the default physical address is 0xFF200000. Before accessing them, the software maps the bridge reset register at 0xFFD0501C and clears bits [1:0], using value &= ~0x3U, to enable the HPS-FPGA bridges.

Q6. Is your custom hardware using AXI or Avalon
A
At the HPS bridge level, the lightweight HPS-to-FPGA bridge is AXI-based. But inside Platform Designer, our custom blocks are connected as Avalon-MM slave peripherals. In the Verilog modules, the interface signals are Avalon-style avs_chipselect, avs_read, avs_write, avs_address, avs_writedata, and avs_readdata.

Q7. Where do these addresses come from Why is VGA at 0xFF240000
A
The lightweight bridge base address is 0xFF200000. In soc_system.qsys, the audio IP has Qsys base offset 0x0000, so its Linux physical address is 0xFF200000. The VGA IP has Qsys base offset 0x40000, so its Linux physical address is 0xFF200000 + 0x40000 = 0xFF240000.

Q8. What is the data width of the MMIO interface
A
Both custom IPs use a 32-bit Avalon-MM data bus. Software reads and writes 32-bit words. For VGA, each 32-bit word contains two RGB565 pixels. For audio, each write to the left or right data register contains one sample word, where software places a signed 16-bit PCM sample in the upper 16 bits.

Q9. How many address bits do your IP blocks use
A
The audio IP uses a 2-bit word address because it only has four registers control, fifospace, leftdata, and rightdata. The VGA IP uses a 16-bit word address because it has a large framebuffer window. The framebuffer starts at word offset 1024 and contains 38400 words, so the highest valid word offset is 39423, which requires 16 bits.

Q10. What does addressUnits WORDS mean in your Platform Designer component
A
It means the address signal passed to the IP is a word offset, not a byte offset. So avs_address = 1 means the second 32-bit register, not byte address plus one. In software, pointer indexing through volatile uint32_t regs naturally uses word offsets, so regs[31] accesses word offset 31.

Section 3 VGA Hardware, About 14 Minutes

Q11. What is the VGA IP register map
A
The VGA IP has control and status registers plus a framebuffer window. Word offset 0 is the control register. Offset 1 returns width, which is 320. Offset 2 returns height, which is 240. Offset 3 returns stride, which is 640 bytes per row. Offset 31 returns the identify value 0x56504741, ASCII VPGA. From word offset 1024 onward, the HPS writes framebuffer data.

Q12. Why do you have an identify register
A
The identify register lets the software verify that the mapped physical address is actually our VGA IP. During initialization, the software reads offset 31 and expects 0x56504741. If the value is wrong, the software assumes the hardware is not present or the address is incorrect, and falls back to another renderer.

Q13. Why is the framebuffer 320 by 240 instead of 640 by 480
A
A 640 by 480 framebuffer would require four times more memory and four times more HPS write bandwidth. We store 320 by 240 in hardware and scale it by 2 in both x and y directions when generating VGA. The output signal is still standard 640 by 480 timing, but each source pixel is repeated as a 2 by 2 block.

Q14. How does the hardware scale 320 by 240 to 640 by 480
A
The VGA timing counters are h_count and v_count. The source pixel coordinate is computed by dropping the low bit: source_x = h_count[9:1] and source_y = v_count[8:1]. That effectively divides both screen coordinates by 2, so each framebuffer pixel is displayed twice horizontally and twice vertically.

Q15. How many framebuffer words are there
A
The framebuffer is 320 by 240 pixels. Each pixel is 16 bits, and each 32-bit word stores two pixels. So the total word count is 320 * 240 / 2 = 38400 words. Since the framebuffer starts at word offset 1024, the valid range is 1024 through 39423.

Q16. Why does the framebuffer start at offset 1024
A
Offset 1024 separates the control register area from the framebuffer data area. It also keeps the framebuffer aligned at a clean 4 KiB boundary because 1024 words times 4 bytes is 4096 bytes. In hardware, this makes the decode simple if the word address is between 1024 and 1024 + 38400, the write goes to framebuffer RAM; otherwise it is treated as a register access.

Q17. What is the pixel format
A
The pixel format is RGB565. Bits [15:11] are red, bits [10:5] are green, and bits [4:0] are blue. The hardware expands red and blue from 5 bits to 8 bits and green from 6 bits to 8 bits before driving the VGA DAC.

Q18. How does software pack two pixels into one word
A
The software backbuffer is byte-addressed. During flush, it takes four bytes at a time. The first two bytes form the low 16-bit RGB565 pixel, and the next two bytes form the high 16-bit RGB565 pixel. Then it writes lo | (hi << 16) to the FPGA framebuffer window.

Q19. Why do you use double buffering
A
Double buffering prevents tearing. The VGA hardware reads from the display buffer while the HPS writes the other buffer. After the HPS finishes writing a full frame, it writes the swap request bit. The hardware waits until vertical blanking and then swaps the display buffer.

Q20. How does the swap request work
A
Software writes bit 1 of the control register. Hardware sets swap_pending. During VGA scanning, when the design reaches the vblank point, specifically when h_count == 0 and v_count == V_VISIBLE on a pixel tick, it toggles display_buffer and clears swap_pending.

Q21. How does software know it is safe to write the next frame
A
Before writing a new frame, software polls the control register and waits until the swap_pending bit is cleared. That means the previous swap has completed. Then it writes the next full frame into the current back buffer and requests another swap.

Q22. What memory blocks are used for framebuffer storage
A
The VGA IP instantiates two altsyncram dual-port memories, one for each framebuffer bank. They are configured as M10K RAMs, 32 bits wide, with 38400 words per bank. Port A is written by the Avalon-MM side, and port B is read by the VGA scanout logic.

Q23. What is the VGA timing
A
The IP generates standard 640 by 480 timing. Horizontally, visible is 640, front porch 16, sync 96, back porch 48, total 800. Vertically, visible is 480, front porch 10, sync 2, back porch 33, total 525. The 50 MHz clock is divided by two using pixel_tick, giving about 25 MHz pixel timing.

Section 4 Audio Hardware, About 10 Minutes

Q24. What is the audio IP register map
A
The audio IP has four word registers. Offset 0 is control/status. Offset 1 is FIFO space. Offset 2 is left channel data. Offset 3 is right channel data. The software writes sample data to offsets 2 and 3, reads offset 1 to know how much FIFO space is available, and writes offset 0 when it needs to clear FIFO or error status.

Q25. What does the control register contain
A
On read, bit 0 means codec initialization is done, bit 1 means codec initialization error, bit 2 means transmit underflow was seen, and bit 3 means write overflow was seen. The upper bytes report FIFO counts: left count in [31:24] and right count in [23:16]. On write, bit 2 clears the read side/status path and bit 3 clears the write FIFO/status path; the software writes both bits during MMIO audio startup.

Q26. What does the fifospace register contain
A
Bits [31:24] contain the left FIFO write space. Bits [23:16] contain the right FIFO write space. Software reads both values and uses the smaller one, so it writes balanced stereo frames.

Q27. Why do you have separate left and right FIFOs
A
The WM8731 output stream is stereo, so the hardware needs a left sample and a right sample for each audio frame. Separate FIFOs make the interface simple software writes left samples to offset 2 and right samples to offset 3. The serializer pops one sample from each FIFO when it starts a new stereo frame.

Q28. What happens if the FIFO is empty
A
If the audio serializer needs data but one of the FIFOs is empty, it outputs zero for that frame and sets the underflow flag. That avoids undefined output and gives software a status bit to debug audio starvation.

Q29. What happens if software writes when FIFO is full
A
The hardware does not push the sample into the FIFO and sets the overflow flag. Software is supposed to check fifospace before writing, so overflow indicates the HPS is writing too aggressively or not respecting the register protocol.

Q30. How is the codec initialized
A
The audio IP contains an I2C FSM. It sends a sequence of configuration words to the WM8731 at I2C address 0x1A. The sequence resets the codec, mutes line input, sets output volume, selects DAC playback, powers up the device, chooses left-justified 16-bit slave mode, sets normal mode, and activates the digital interface.

Q31. What sample rate do you output
A
With a 50 MHz FPGA clock, BCLK_DIV = 16, so BCLK is 3.125 MHz. Each stereo frame has 64 bits, so the LRCK rate is about 3.125 MHz / 64 = 48.828125 kHz. The current software MMIO path resamples WAV files to 48 kHz, which is close to this hardware rate.

Q32. Why is the sample written as int16 << 16
A
The hardware sends the most significant bits first in a 32-bit slot, while the codec is configured for 16-bit left-justified samples. By placing the signed 16-bit sample in the upper 16 bits, the serializer outputs the sample in the expected left-justified position.

Section 5 Input Pipeline, About 7 Minutes

Q33. How do you read player input
A
On the HPS, we use libusb to find USB HID boot keyboards. The manager can open up to two keyboards, one for each player. It reads interrupt IN reports from each keyboard and stores the last report if a poll times out.

Q34. What does a USB keyboard report look like
A
A boot keyboard report contains modifier bits and up to six keycodes. Our helper functions can clear a report, add a keycode for scripted testing, and check whether a keycode is present.

Q35. What is your key mapping
A
W is jump, A is left, D is right, S is crouch, J is attack, K is guard, and L exits the current match back to the menu. Attack combinations are interpreted by the input parser A+J is fireball, D+J is dragon punch, W+J is jump attack, W+D+J is forward jump attack, W+A+J is back jump attack, and S+J is sweep.

Q36. How do you distinguish a held key from a newly pressed key
A
The parser stores the previous button state. A pressed event is current && !previous. Held state is just current. Movement and guarding use held states, while attacks, menu confirmation, jump start, and exit use pressed edges.

Q37. Why do you use edge detection for attacks
A
Without edge detection, holding J would start a new attack every frame. Edge detection makes one key press correspond to one attack command. The game also has attack cooldown and attack phases, so attacks cannot be spammed every frame.

Q38. What happens if two opposite directions are held
A
The parser resolves left/right by requiring one direction but not both. So move_left = left && !right, and move_right = right && !left. If both are pressed, horizontal movement cancels out.

Section 6 Game Logic, About 13 Minutes

Q39. What is the top-level game state machine
A
There are three top-level states MENU, PLAYING, and GAME_OVER. In MENU, any player input starts the round. In PLAYING, the game updates player movement, attacks, projectiles, collision, HP, and timer. In GAME_OVER, the game waits until the game-over animation is ready, then allows restart or return to menu.

Q40. How do you initialize a round
A
The players are reset to starting positions player 1 at one quarter of the screen and player 2 at three quarters. Both are placed on the ground, HP is set to max HP, facing directions are set toward each other, attacks and projectiles are cleared, the round timer is reset to 99 seconds at 60 frames per second, and the winner is cleared.

Q41. How do you update player facing
A
The game compares the center x positions of the two players. If player 1 is left of player 2, player 1 faces right and player 2 faces left. If they cross, the facing directions flip.

Q42. How does movement work
A
If a player is not locked by attack, hit stun, block stun, or KO, the input can set velocity. Walking changes x by walk_speed, which defaults to 3. Jumping sets vertical velocity to -14, and gravity adds 1 each frame. Horizontal jump velocity can be set at jump start. The player position is clamped to the screen bounds.

Q43. What does “controls locked” mean
A
Controls are locked if the player has no HP, is in hurt visual frames, is in block stun, or is currently in an attack phase. In those cases, new movement or new attack commands are ignored. This prevents animation and combat states from being interrupted incorrectly.

Q44. What is the attack state machine
A
Each attack goes through phases. It starts in STARTUP, then moves to ACTIVE. If it hits, it goes to HIT_CONFIRM; if it is blocked, it goes to BLOCK_CONFIRM; if it misses, it goes to RECOVERY. After recovery, it returns to NONE. Each attack profile defines how many frames each phase lasts. Fireball is a special case: the normal melee contact evaluator skips it, and the active phase spawns a projectile instead.

Q45. What are attack profiles
A
An attack profile contains reach, damage, startup frames, active frames, recovery frames, hit confirm frames, block confirm frames, hit stun frames, and block stun frames. For example, normal attack is 48 reach and 12 damage, fireball is 96 reach and 18 damage, and dragon punch is 56 reach and 20 damage plus a lift velocity when it becomes active.

Q46. How do you detect a normal hit
A
The attacker must be in ACTIVE phase and must not have already connected. The game checks the horizontal distance from the attacker’s front side to the target center and compares it against the attack reach. It also checks vertical overlap. If the target can guard, the result is blocked; otherwise it is a hit.

Q47. How do you prevent one attack from hitting multiple times
A
Each player has attack_has_connected. Once an attack hits or is blocked, this flag is set. The contact evaluator ignores further hits from that same attack. There is also a test that verifies a normal attack only subtracts HP once.

Q48. How do projectiles work
A
Fireball attack creates a projectile when the attack transitions into the ACTIVE phase. The projectile has an owner, position, velocity, character ID, and animation tick count. Every frame it moves by projectile_speed, which defaults to 6 pixels per logic frame. If it leaves the screen, it is reset. If it overlaps the opponent, it applies hit or block damage and disappears.

Q49. What happens when two fireballs collide
A
If both players have active projectiles and their projectile rectangles overlap, both projectiles are reset. No player takes damage. There is a test for this behavior.

Q50. How does blocking work
A
A player can guard if guard is held, the player is grounded, alive, not attacking, not in hit stun, and not in block stun. If an incoming attack is blocked, the target takes chip damage, enters block stun, and the attacker enters block confirm. If the target is also crouching, the visual state becomes crouch guard.

Q51. How does a round end
A
If one player’s HP reaches zero, the other player wins by KO. If both reach zero, it is a draw or double KO. If the round timer reaches zero, the player with more HP wins; if HP is equal, the result is a draw.

Q52. Why does game-over not accept restart immediately
A
The game waits for game_over_anim_frames, which defaults to 120 frames. This gives time for KO or victory animation before accepting restart. The test test_game_over_restart_gate verifies that pressing attack too early does not restart.

Section 7 Animation and Rendering, About 7 Minutes

Q53. Where do the sprites come from
A
Sprites are stored as PPM files under game_assets/sprites/RyuPPM and game_assets/sprites/KenPPM. Each action has a directory, such as idle, walk, attack_fireball, attack_dragon_punch, hit, ko, and so on. On the board the loader first tries /root/game_assets/sprites/..., and in the repo it falls back to ../game_assets/sprites/.... The animation system scans these directories, sorts the PPM files, and loads them into animation clips.

Q54. Why use PPM files
A
PPM is simple to parse in C without external image libraries. The loader can read P3 or P6 PPM format, extract width, height, and RGB pixels, and then the renderer converts or draws them into the framebuffer.

Q55. How does the animation system choose a clip
A
The gameplay layer sets each player’s visual_state, last_attack, velocity, facing, and character ID. The animation system maps those fields to a clip. For example, if visual state is ATTACK and last attack is FIREBALL, it chooses the fireball attack clip. If visual state is JUMP, it uses velocity and facing to choose neutral, forward, or backward jump animation.

Q56. What happens when a non-looping animation reaches the last frame
A
If the clip is non-looping, the animation state stays on the last frame. This is important for crouch, KO, victory, and some attack animations. There is a test verifying that non-looping hold animations stop on the last frame.

Q57. How does the renderer draw a frame
A
The renderer first clears or draws the background. Then it draws the players using their current animation sprites, draws projectiles, draws HP bars, player labels, and the timer. In menu and game-over states it draws different screens. After drawing into the software backbuffer, it flushes the frame to MMIO VGA or Linux framebuffer.

Q58. What renderer backends do you have
A
There are three backends. First, MMIO VGA, which uses our FPGA VGA IP. Second, Linux framebuffer, such as /dev/fb0 or /dev/fb1, as a fallback. Third, console renderer, which prints game state and player state changes for debugging when graphical output is not available.

Section 8 Current Display MMIO Interface, About 4 Minutes

Q59. Does the FPGA consume game-state registers for rendering
A
No. The old game-state register encoder path has been removed. In the current design, the HPS renders complete RGB565 pixels in software and writes them into the VGA framebuffer window. The FPGA display IP only consumes control, geometry, ident, and framebuffer registers.

Q60. What is at VGA word offset 0 now
A
Word offset 0 is the VGA control register. Software reads it to check swap status and writes bit 1 to request a buffer swap at the next vblank. It is not a game-state word.

Section 9 Verification, About 9 Minutes

Q61. How did you test the game logic
A
We have an automated C test program, phase1_test. It tests menu transition, exit back to menu, knockout transition, game-over restart delay, timeout result, hit confirm, block stun, crouch guard, jump restrictions, overlap resolution, dragon punch lift, attack movement lockout, fireball hit, fireball cancellation, fireball boundary disappearance, and animation behavior.

Q62. How did you test VGA independently
A
We have a VGA probe program. It maps the bridge reset register, enables the bridge, maps the VGA IP, checks the VPGA ident register, verifies width, height, and stride, writes a known RGB565 gradient into the framebuffer window, and requests a buffer swap. This isolates the VGA hardware path from the game logic.

Q63. How did you test audio independently
A
We have an audio probe program that maps the audio IP, reads the control and FIFO space registers, and checks whether the FIFO space is reasonable. The full audio path then loads WAV assets, resamples them, and runs a thread that fills the hardware FIFO.

Q64. How do you know the HPS-to-FPGA bridge is working
A
Both VGA and audio initialization first enable the bridge by clearing bits [1:0] of 0xFFD0501C. Then the software reads known hardware values: VGA ident VPGA plus geometry registers, or audio control/FIFO space. If those reads return expected values, the MMIO path is working.

Q65. What happens if MMIO is not working
A
The software does not crash immediately. For rendering, it falls back from MMIO VGA to Linux framebuffer or console. For audio, the default context first tries the WM8731 MMIO path and can use command-line players like aplay or ffplay when command audio is enabled. This makes debugging easier because gameplay can still run even if one hardware path is not ready.

Q66. What are the most important tests you would show during demo
A
I would show phase1_test to prove gameplay logic, vga_probe to prove the VGA IP and framebuffer path, and audio_probe or audio_demo to prove the audio register interface. Then I would run phase1_demo --script smoke or phase1_demo --script ko for a deterministic integrated demo. If libusb is available on the board, phase1_demo --usb shows the same loop with real keyboard input.

Section 10 Design Tradeoffs and Limitations, About 7 Minutes

Q67. What is the biggest limitation of your current design
A
The biggest limitation is that the HPS writes a full framebuffer every frame. This is simple and flexible, but it uses more bridge bandwidth than a hardware sprite renderer would. A future version could send only game-state registers to the FPGA and let hardware compose sprites.

Q68. Why did you not render sprites fully in hardware
A
Hardware sprite rendering would require sprite memory management, transparency handling, scaling or clipping, animation frame selection, and possibly asset conversion into FPGA memory. Given the project schedule, using the HPS for rasterization and FPGA for scanout was more reliable and easier to verify.

Q69. Is the audio sample rate exact
A
It is close but not exact. With the current dividers, LRCK is approximately 48.828 kHz. Software resamples to 48 kHz, so there is a small mismatch. For a more precise design, we could choose clocking or codec configuration that produces exactly 48 kHz or adjust software resampling to the exact hardware rate.

Q70. Does the project require root access
A
For MMIO access through devmem, yes, typically it requires root or suitable permissions. The software maps physical addresses directly, which is common for board bring-up but not ideal for a production Linux application.

Q71. What would you improve if you had more time
A
I would clean up the documentation encoding, remove or rename old game-state MMIO constants to avoid confusion with the framebuffer control register, add hardware simulation testbenches for VGA and audio, make the audio clock exactly match the software sample rate, and possibly implement a hardware sprite compositor to reduce HPS-to-FPGA bandwidth.

Q72. What is your strongest evidence that the system is correct
A
The strongest evidence is layered verification. The pure game logic is tested with automated unit-style tests in sw/tests/test_phase1.c. The VGA MMIO interface is tested with an independent probe using the VPGA ident register, geometry registers, and gradient output. The audio interface is tested with FIFO-space probing and WAV playback. The integrated phase1_demo then combines input, game logic, animation, rendering, and audio commands.

Closing Summary, About 2 Minutes

Q73. Give me a short summary of your design.
A
Our design is a DE1-SoC fighting game where the HPS runs the game engine and the FPGA provides custom VGA and audio peripherals. The HPS reads scripted or USB keyboard input, updates the game state at about 60 Hz, renders a 320 by 240 RGB565 framebuffer, and sends it through MMIO to the VGA IP at 0xFF240000. The VGA IP double-buffers the frame and generates 640 by 480 VGA output. For audio, the HPS streams PCM samples to an MMIO FIFO at 0xFF200000, and the audio IP initializes and drives the WM8731 codec. We verify the system with software tests, VGA/audio probe programs, and integrated demos.

Q74. If I only remember one thing about your project, what should it be
A
The key idea is that we built a complete HPS-to-FPGA game pipeline: software game engine on the HPS, custom FPGA VGA framebuffer output, custom FPGA audio streaming, and keyboard control. The important engineering part is not just the game itself, but the clearly defined MMIO contract between software and hardware.
