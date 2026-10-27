# Native C++ runtime and portrait fitter

The native path is C++17 and offline-only. It does not start a browser, execute JavaScript, use Chromium, or download runtime assets.

- `gtc-native-client` connects to the authoritative server and renders its replicated world in a resizable window that opens at **480×640**.
- `gtc-native-smoke` runs two real networked citizens through replication, customization, movement, pathfinding, and toilet-action checks.
- `gtc-native-fit` is the portrait fitting and review tool; it does not call a landmark detector during optimization.

## Build

Linux (ccache is selected through the repository's content-hash configuration):

```sh
./native/build.sh
```

Windows with Visual Studio 2022 and the **Desktop development with C++** workload:

```cmd
native\build.cmd
```

`native/build.ps1` is the PowerShell wrapper. The command uses `vswhere`/`vcvars64.bat` to find MSVC v143. Linux executables are retained under `prebuilt/linux-x86_64/` with `prebuilt/NATIVE_SHA256SUMS`.

## Native multiplayer client and smoke test

For the automatic launcher, double-click **`run.cmd`** on Windows or run **`./run.sh`** on Linux.
Both build the server and native client, wait for the authoritative port, and open the native GUI.

To start the pieces manually from the repository root:

```sh
./build/bin/gtc1453 --port 1453 --web web
./build/bin/gtc-native-client --name "Native Citizen"
```

The client uses the existing `/ws` welcome/world, state, input, path, toilet, customize, and cheat messages. It sends no positions and has no offline player-only action path. Toilet requests never auto-walk.

The default is a resizable native Win32 window on Windows or a dynamically loaded **system X11 window** on Linux. Both platforms use the same fixed-function OpenGL scene renderer: WGL on Windows and dynamically loaded GLX/OpenGL on Linux. It provides textured triangle batches, cached texture uploads, GPU depth testing, direct scene presentation, and a CPU-composited native HUD uploaded as an alpha-blended overlay texture. Vertex buffer objects are used when available, with client-array fallback. The world-space scene and its VBO contents are retained across camera frames and rebuilt only for relevant simulation/view changes; camera movement and resizing only update matrices. Three rotating textures present the CPU-composited native HUD without normal-frame scene readback. On OpenGL 2.0-capable drivers, a GLSL 1.10 material path adds tangent-space skin normal maps, two-sided diffuse/specular lighting, rim response, fog, ACES-style tone mapping, and sRGB output; unsupported or failed shader setups retain the fixed-function path. On OpenGL 3+ drivers with vertex texture fetch, retained rest meshes use four-influence GPU skeletal skinning and per-citizen float bone textures, so walking and seated poses update continuously without rebuilding or re-uploading body VBOs. Older drivers retain CPU-posed mesh rebuilds. Full-frame readback happens only for an explicit F12 screenshot. The native window title reports the actual OpenGL vendor/renderer and whether `GLSL skin`, GLSL, or fixed-function materials are active. The interactive loop has no sleep-based frame cap and requests uncapped swaps through standard WGL/GLX extensions when available. Authoritative citizen snapshots are interpolated at 15 Hz response, with 16 Hz third-person and 12 Hz god-camera smoothing; first-person remains directly attached. Per-character GPU model matrices move the retained meshes with that interpolated state, while gameplay remains server-authoritative. Set `GTC_GL_DEBUG=1` to enable per-frame OpenGL error checks (otherwise those synchronization-prone checks stay out of the hot path). The HUD reports `GPU` only when that context is active. If GLX/OpenGL is unavailable, Linux retains its CPU raster/`XPutImage` fallback; Windows retains its GDI fallback. Linux has no X11 or OpenGL link-time/development-package dependency and no downloaded windowing runtime. Headless captures use the CPU renderer and do not validate a GPU context. On a machine without a display, use `--headless`; useful headless options include `--frames N`, `--benchmark N`, `--width`, `--height`, `--mode tps|fps|god`, `--path X Z`, `--move X Z`, `--toilet sit|shit|piss`, `--yaw`, `--pitch`, and `--output PNG`.

Native controls: **1/2/3** select TPS/god/FPS; WASD moves or fast-pans god view; Shift sprints/boosts; click captures the mouse and Esc releases it; the wheel zooms; **hold F** to piss in FPS; **E** opens the in-range Sit/Shit/Piss wheel or stands; god-view floor clicks explicitly pathfind. **C** opens the generic style + 107-universal-morph creator (arrows, Page Up/Down, Enter to replicate), **F1** opens cheats, **H** opens help, and **F12** saves a native PNG. The proportional sans UI renders at least 20 pixels high and scales with the logical 480×640 layout.

To review the exact native overlay pixels without a display:

```sh
./build/bin/gtc-native-client --review-ui build/native-ui-review
```

With the server running, execute the two-player test:

```sh
./build/bin/gtc-native-smoke --output build/native-smoke
```

It fails unless both clients replicate distinct identities, universal morph/style changes, server movement, a replicated path, and a server-granted seated Shit action. It writes `report.json` plus inspected 480×640 TPS, FPS, and god-view PNGs. The renderer uses the CC0 full-body mesh and rig weights, all replicated universal morphs, generic skin maps and MHCLO hair, near-plane clipping, camera obstruction handling, and authoritative puddles/actions. No portrait texture or person-specific sculpt is used.

## Portrait fitter

From the repository root:

```sh
./build/bin/gtc-native-fit --iterations 8
```

Fit one or more named presets, or render the authoritative native presets without fitting:

```sh
./build/bin/gtc-native-fit --render-only marilyn obama
./build/bin/gtc-native-fit --no-render xi
./build/bin/gtc-native-fit --direct --output build/native-direct marilyn
```

The review resolution is deliberately fixed at **480×640**. Outputs are written after each person to `build/native-fit/`:

- `NAME-initial.png` and `NAME-final.png` for visual A/B inspection;
- `results.json` for initial/final numeric error, evaluations, and timing;
- `presets.json` for the candidate universal values.

A candidate is **not** promoted automatically. Lower landmark error is not likeness acceptance; inspect the native renders and reject distortion, scalp/eye artifacts, or a worse silhouette.

## Inputs and method

- `native/presets.json` is the authoritative native source for the seven people and their camera poses. It contains only universal morph values and generic style choices.
- The model is read directly from the vendored CC0 MakeHuman OBJ, skeleton, and universal target files. No person-specific sculpt targets or portrait textures are used.
- Canonical landmark correspondences are built once per sex with bounded anisotropic alignment and closest-triangle barycentric mapping. Each trial evaluates deformed surface points and a perspective camera numerically: coarse pose search, bounded Levenberg–Marquardt with a prior penalty, then integer-slider refinement. `--direct` retains the nearest-vertex mapping for A/B regression checks.
- The CPU rasterizer has perspective-correct UV interpolation, a depth buffer, generic skin detail/normal maps, procedural eyes, MHCLO barycentric hair, alpha-tested generic hair textures, and a parametric scalp/hairline pass. Head/neck face selection uses the vendored CC0 skeleton weights rather than a person-specific cut.
- The reference landmark JSON is development fitting data only. It is never sampled as an image or transferred to a material.

The preserved browser implementation is a legacy checkpoint only. Native networking, GPU/CPU world rendering, interactive Win32/X11 window/input, native overlays, and the headless multiplayer driver are implemented here. Actual native PNG review remains required for visual acceptance.

## Vendored decoder

`native/vendor/stb_image.h`, `stb_image_write.h`, and `stb_truetype.h` are from `nothings/stb`, commit `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5`. stb is dual-licensed under MIT or public domain; see `native/vendor/README.md`. These third-party headers are exempt from the authored-file line nagger. `native/assets/ui-sans.ttf` is unmodified DejaVu Sans; its Bitstream Vera/DejaVu redistribution notice is retained in `native/assets/FONT-LICENSE.txt`.
