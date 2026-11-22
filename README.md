# SharkOS Game

A semi-3D stickman platformer with Undertale-style bullet hell, booting straight into the game on the SharkOS kernel.

**Original SharkOS by [mayshecry](https://github.com/mayshecry/Shark-OS)** — stripped, rebuilt and turned into a game.

## Run

```sh
make            # builds sharkos-game.bin (95K)
make iso        # builds sharkos-game.iso (4.7M)
```

```sh
qemu-system-x86_64 -m 512 -smp 2 -cdrom sharkos-game.iso -boot order=d -vga std \
  -netdev user,id=net0 -device e1000,netdev=net0
```

The ISO is a Limine BIOS/UEFI image: it also boots from a USB stick or a VM with no setup.

## Input

Keys are polled from the keyboard driver as real down/up state (`keyboard_down()` in `src/drivers/keyboard.c`), not one-shot press events, so holding WASD walks continuously and jumping/ shooting are edge-triggered so a held key does not repeat.

## Play

| Key | Action |
| --- | --- |
| WASD / arrows | Move |
| Space | Jump |
| Z | Talk to NPCs |
| X | Shoot |
| T | Chat (P2P) |
| I | Type a peer IP for direct P2P |
| Enter | Start |
| Esc | Menu / shutdown |

Collect 5 hearts to win. Dodge the bullets, shoot the enemy stickmen, stay determined.

## Multiplayer

Peer-to-peer over the networking built into SharkOS (`src/net`):

- UDP port **31337**, broadcast HELLO/POS/CHAT packets plus unicast to a manually entered IP
- DHCP comes up automatically at boot — the title screen shows `NET READY IP <addr>`
- Peers expire after 6 s without a packet; each peer shows up as a remote stickman with its own chat

Same LAN, no server, no relay.

## What was stripped

Everything that is not needed to boot and play was removed from upstream SharkOS: the desktop, terminal UI chrome, window manager, file system, shell, editor, browser, HTTP/TLS server, package manager, apps and their assets.

Kept: the kernel boot path (Limine multiboot1, `boot.s`, GDT/IDT/PIC/PIT), paging and the PMM, PCI + e1000 driver, the SharkOS network stack (ARP/IPv4/UDP/DHCP/DNS/TCP), the framebuffer and font renderer, keyboard and RTC drivers.

```
src/arch/    early boot, io, cpu, mtrr, interrupts
src/drivers/ pci, rtc, keyboard
src/lib/     lib, pmm, globals
src/net/     net, vnic, firewall, crypto, tls, ed25519
src/game/    fb, game, main
```

`kmain` in `src/game/main.c` goes: serial → framebuffer → PMM → IDT/PIC/PIT → keyboard → RTC → net init → DHCP → `game_run()`.

## Renderer

Two layers, no VSync, dirty-rect updates:

- The sky, city, ground, platforms, hearts and HUD are rendered once into a cached background buffer and republished only when the camera moves, the HUD changes or something is hit.
- Every frame only the moving entities are touched: their previous rectangles are restored from the cached layer and the sprites are drawn straight into VRAM. No full-screen blit, no back-buffer copy, no present pass.
- The loop runs logic on a fixed 16 ms step and renders uncapped; `hlt` is only issued once every 32 frames, so the timer keeps advancing while the renderer spins.

Measured in QEMU (TCG, 1024x768x32, `-smp 2`), idle gameplay: **~6000-7500 FPS**, and **~4000 FPS** while walking with the camera scrolling. The counter on the HUD counts frames that were actually composed and written to the framebuffer.

Hot-path details: no per-pixel division (framebuffer stride cached), midpoint circle rasteriser, span fills instead of per-pixel hearts, per-entity culling against the viewport, and `rep movsq` rather than hand-written loops (QEMU emulates string ops faster than unrolled 32-bit stores).

Caveat: uncapped means the CPU spins at 100% - that is what "no frame cap" costs. The visible refresh rate is still bounded by the display adapter.

## Look and feel

The world uses a single perspective projection: depth drives both scale and vertical placement, so the ground plane recedes to the horizon, ledges far away sit higher and smaller, and near ones drop toward the bottom of the screen. The floor is a real plane with depth bands, converging grid lines and scattered detail that scroll past; the city sits behind it in two parallax layers.

Articulated stickmen with two-segment arms and legs, walk cycle, body bob, tucked jump pose and ground shadows. A particle system drives landing dust, jump puffs, footsteps, muzzle sparks, damage bursts and heart sparkles. The world has a glowing moon, twinkling stars, a shooting star, two parallax layers of lit towers, floating ledges with visible top faces and rim-lit edges, a pulsing menu panel and screen shake on damage.

## Screenshots

`screenshot_title.png`, `screenshot_play.png`, `screenshot_jump.png`

## Credit

Original kernel and networking: **mayshecry** — https://github.com/mayshecry/Shark-OS
