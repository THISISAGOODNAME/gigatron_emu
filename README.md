# Gigatron TTL Microcomputer Emulator

![sokol demo](screenshots/Snipaste_2026-01-13_13-02-01.png)
sokol demo

![raylib demo](screenshots/Snipaste_2026-01-13_14-04-57.png)
raylib demo

A cross-platform emulator for the [Gigatron TTL microcomputer](https://gigatron.io/), implemented in C with a sokol + Dear ImGui frontend.

Based on https://github.com/PhilThomas/gigatron.
You can [try it online](https://gigatron.io/emu/).

You can find roms & apps at https://github.com/kervinck/gigatron-rom/

## Features

- **Pure C emulator core** - Clean, portable implementation of the Gigatron CPU, VGA output, and audio
- **Cross-platform GUI** - Built with sokol and Dear ImGui, runs on Windows, macOS, and Linux
- **GT1 file loading** - Load and run GT1 programs
- **Debug tools** - CPU state viewer, memory viewer, step debugging
- **Audio emulation** - Real-time audio output via sokol_audio

## Usage

1. Launch the emulator: `./gigatron_emu`
2. Load a ROM file: `File > Open ROM...` (or drag & drop a .rom file)
3. Load GT1 programs: `File > Load GT1...` (or drag & drop a .gt1 file)

### Controls

| Key | Button |
|-----|--------|
| Arrow Keys / WASD | D-Pad |
| Z / J | A Button |
| X / K | B Button |
| Enter | Start |
| Escape / Backspace | Select |

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+O | Open ROM |
| Ctrl+L | Load GT1 |
| F1 | Toggle Debug Window |
| F2 | Toggle CPU State |
| F3 | Toggle Memory Viewer |
| F5 | Reset Emulator |
| Space | Pause/Resume |

## Architecture

The emulator is structured in layers:

```
┌─────────────────────────────────────┐
│         Frontend (main.cpp)         │
│       sokol_app + Dear ImGui        │
├─────────────────────────────────────┤
│  VGA   │   Audio   │    Loader      │
│ vga.c  │  audio.c  │   loader.c     │
├─────────────────────────────────────┤
│         Gigatron Core               │
│           gigatron.c                │
└─────────────────────────────────────┘
```

### Core Components

- **gigatron.c/h** - CPU emulation (registers, instruction decoding, execution)
- **vga.c/h** - VGA signal generation and framebuffer rendering
- **audio.c/h** - Audio sample generation from OUTX register
- **loader.c/h** - GT1 file parser and loader

## Technical Details

### Gigatron Specifications

- **CPU Clock**: 6.25 MHz
- **ROM**: 64K x 16-bit (128KB)
- **RAM**: 32K x 8-bit (32KB)
- **Display**: 640x480 VGA (160x120 Gigatron pixels)
- **Audio**: 4-bit DAC via OUTX register
- **Input**: 8-bit active-low gamepad interface

### Instruction Format

```
15 14 13 | 12 11 10 | 9 8 | 7 6 5 4 3 2 1 0
   OP    |   MODE   | BUS |        D
```

- **OP** (3 bits): Operation (LD, AND, OR, XOR, ADD, SUB, ST, BR)
- **MODE** (3 bits): Addressing mode / destination register
- **BUS** (2 bits): Bus source (D, RAM, AC, IN)
- **D** (8 bits): Immediate data

## Resources

- [Gigatron Official Website](https://gigatron.io/)
- [Gigatron GitHub](https://github.com/kervinck/gigatron-rom)
- [Gigatron Forum](https://forum.gigatron.io/)

## License

MIT License - See LICENSE file for details.

## Credits

Based on the official Gigatron JavaScript emulator and documentation.
