# TurtleFX — Modern Logo Shader Engine

> **Made with Gemini Neon** ✨  
> 📖 **[Interactive Tutorial & Browser Playground (GitHub Pages)](https://xyzzyapps.github.io/turtlefx/)**

A lightweight **C11 + raylib 5.5** game renderer scripted in a **modern Logo dialect** whose entire OpenGL 3.3 GPU shader pipeline — **Retro Pixel**, **2D Vector SDF**, **3D SDF Raymarching**, and **Cross-Mode Post-FX** — is completely abstracted away from the user.

![TurtleFX Showcase](assets/screenshot.png)

---

## Highlights

- **4 Rendering Styles, Zero GLSL Written by the User**
  1. **Retro Pixel (`mode retro(320, 180)`)** — Integer-snapped pixel turtle, ear-clipped concave polygon fills, indexed palettes (`pico8`, `gameboy`, `cga`, `sunset`), and CRT barrel/scanline shaders.
  2. **Modern 2D (`mode 2d`)** — Resolution-independent 2D Signed Distance Field strokes & shapes, exponential neon glow halos, additive blending, and 2-pass separable Gaussian bloom.
  3. **3D Raymarching (`mode 3d`)** — Full 3D orthonormal turtle frame (`pitch`, `yaw`, `roll`), 3D SDF primitives (`sphere`, `capsule`, `box`, `cylinder`, `cone`, `torus`), polynomial smooth blending (`smooth`), boolean subtraction (`carve`), soft shadows, ambient occlusion, and emissive materials.
  4. **Cross-Mode Post-FX** — Route any 2D or 3D Logo scene through `fx retro(...)`, 4×4 Bayer ordered dithering, distance fog, bloom, CRT curvature, and vignette in two lines of Logo.
- **Live Hot-Reloading** — Edit and save any `.logo` script while the engine is running to update the scene instantaneously with on-screen syntax/runtime error overlays.

---

## Build & Run

Requires **CMake 3.20+**, a C11 compiler (`gcc` / `clang` / MSVC), and **raylib 5.x**.

```powershell
cmake -B build -G Ninja
cmake --build build
.\build\turtlefx.exe
```

Run a specific `.logo` script or headless test suite:

```powershell
.\build\turtlefx.exe examples\03_3d_crystal_tree.logo
.\build\turtlefx.exe --verify
```

---

## Controls

| Key | Action |
|---|---|
| `1` – `4` | Switch live between built-in examples (`Retro Arcade`, `Neon 2D`, `3D Crystal Tree`, `Retro 3D`) |
| `Left` / `Right` (`A` / `D`) | Steer ship in `01_retro_arcade.logo` |
| `Space` | Fire lasers & destroy enemy ships in `01_retro_arcade.logo` |
| `R` | Reload active `.logo` script manually (also auto hot-reloads on file save) |
| `H` | Toggle HUD overlay |
| `F12` / `P` | Save in-game screenshot (`screenshot_001.png`, ...) |

---

## Modern Logo Quick Reference

```logo
mode 3d
sky gradient(#0b1120, #312e81)
light sun(dir: [-1, -2, -1], color: #ffe8cc, shadows: soft)
fx bloom(strength: 0.55, radius: 10)

to tree(len, radius, depth)
  if depth == 0 [
    material emissive(#38bdf8, strength: 2.2)
    sphere(radius * 2.4)
    stop
  ]
  material matte(#6b4423)
  capsule(len, radius)
  fd len
  branch [ roll 15   pitch 28  tree(len * 0.72, radius * 0.72, depth - 1) ]
  branch [ roll 135  pitch 30  tree(len * 0.70, radius * 0.72, depth - 1) ]
  branch [ roll 255  pitch 26  tree(len * 0.68, radius * 0.72, depth - 1) ]
end
```

- **Modes & Shaders**: `mode retro(w, h) | 2d | 3d`, `palette pico8 | gameboy | cga | sunset`, `fx crt(...)`, `fx bloom(...)`, `fx retro(...)`, `fx fog(...)`, `fx vignette(...)`
- **2D / Retro Turtle**: `fd`, `bk`, `rt`, `lt`, `pu`, `pd`, `dot`, `circle`, `rect`, `fill [ ... ]`, `color`, `pen_width`, `glow`, `blend add | alpha`
- **3D Turtle & SDF**: `pitch`, `yaw`, `roll`, `sphere`, `capsule`, `box`, `cylinder`, `cone`, `torus`, `material matte | glossy | emissive(...)`, `smooth k [ ... ]`, `carve [ ... ]`, `light sun(...)`, `camera(...)`
- **Scoped Transforms**: `branch [ ... ]`, `at x y [ ... ]`, `at x y z [ ... ]`, `offset x y [ ... ]`
- **State & Control Flow**: `let x = 0`, `let arr = [1, 1, 1]`, `arr[i]`, `if ... [ ... ] else [ ... ]`, `repeat N with i [ ... ]`, `while ... [ ... ]`, `to name(...) ... end`, `on frame(dt) ... end`

---

## Architecture

```text
 .logo Script ──► Lexer/Parser (AST) ──► Logo VM & 2D/3D Turtle ──► SceneIR ──► RGBA32F Data Textures ──► 5 GLSL 330 Shaders
```

- **`src/logo_parser.c`** — Lexer and recursive-descent AST parser supporting chained bare commands, `[ ... ]` scoped blocks, list indexing, and named shader parameters.
- **`src/logo_vm.c`** — Executes scripts each frame, maintains 2D/3D orthonormal turtle state, ear-clips concave `fill` polygons, and emits flat `Prim2D` / `Prim3D` buffers.
- **`src/renderer.c` & `src/shaders.h`** — Uploads primitives via `RGBA32F` floating-point textures (`texelFetch`) into hidden OpenGL 3.3 SDF, raymarching, bloom, and post-FX shaders.

---

## Credits & Attribution

- **Author**: **Xyzzy Apps** ([@xyzzyapps](https://github.com/xyzzyapps) · `xyzzyapps@gmail.com`)
- **Built with**: Made with **Gemini Neon**

## License

Licensed under the **[PolyForm Noncommercial License 1.0.0](LICENSE)**.  
Copyright © 2026 Xyzzy Apps. Noncommercial use only — see [LICENSE](LICENSE) for full terms.
