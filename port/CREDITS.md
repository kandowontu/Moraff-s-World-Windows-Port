# Credits

## Moraff's World

**Original game creator, designer, and programmer:** Steve Moraff
**Original publisher:** MoraffWare

Moraff's World, its name, gameplay, world, characters, writing, graphics,
fonts, maps, executable code, and original data are credited to Steve Moraff /
MoraffWare. Their original work is the reason this preservation project
exists.

All copyrights and other rights in the original game remain with their
respective owner. No original-game asset is relicensed by this project.

## Native port and preservation project

**Reverse-engineering project and native port:** kandowontu

**Port copyright:** Copyright © 2025 kandowontu

**Development assistance:** OpenAI Codex

The native implementation was reconstructed through analysis of the original
DOS executable, annotated assembly, decompiled pseudocode, original data
formats, and direct behavioral comparison with the original game.

Enhanced Edition content—including expanded floors, optional late-game
progression, additional variants, equipment, spells, relics, quests, native
tools, diagnostic functions, and quality-of-life behavior—is fan-created port
content and is not original MoraffWare material.

## Open-source software

The executable uses **Simple DirectMedia Layer (SDL) 2.30.12**, created by Sam
Lantinga and SDL contributors. SDL handles the native window, graphics
presentation, keyboard, mouse, timers, and audio device access.

See `THIRD_PARTY_NOTICES.md` for SDL's complete license notice and build-tool
acknowledgements.

## Build tools

This Windows build was produced with:

- GNU Compiler Collection / GCC 13.2.0
- MinGW-w64 x86-64 UCRT toolchain
- CMake 3.29.2
- Ninja 1.12.0

These tools are not redistributed as applications in the release package.
The executable dynamically uses only Windows system libraries and the
Universal C Runtime; SDL is statically linked.

## Independence statement

This is an unofficial fan preservation project. It is not affiliated with,
authorized by, or endorsed by Steve Moraff or MoraffWare.
