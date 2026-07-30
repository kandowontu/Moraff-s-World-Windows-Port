# Third-party notices

## Simple DirectMedia Layer (SDL) 2.30.12

Project: <https://www.libsdl.org/>

Source release used:
<https://github.com/libsdl-org/SDL/releases/tag/release-2.30.12>

SDL is statically linked into `moraffs_world.exe`.

### SDL license

```text
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## Build-tool acknowledgements

The following open-source tools were used to produce the Windows executable.
They are build tools and are not shipped as separate programs in this package.

- **GNU Compiler Collection 13.2.0** — Free Software Foundation; GPLv3.
  Runtime components are distributed under their applicable licenses,
  including the GCC Runtime Library Exception.
  <https://gcc.gnu.org/>
- **MinGW-w64** — Windows headers, CRT/import libraries, and toolchain support
  under the licenses carried by the MinGW-w64 project.
  <https://www.mingw-w64.org/>
- **CMake 3.29.2** — Kitware and contributors; BSD-3-Clause.
  <https://cmake.org/>
- **Ninja 1.12.0** — Ninja contributors; Apache License 2.0.
  <https://ninja-build.org/>

## Windows components

The executable imports standard Microsoft Windows system libraries and the
Universal C Runtime supplied by Windows. No Microsoft DLL is bundled in this
package.

## Original Moraff's World material

Steve Moraff / MoraffWare's original game files are not third-party
open-source components. They remain copyrighted proprietary game material,
are not included in this release, and are not covered by any license above.
