# Native C++ runtime and portrait fitter

The native path is C++17 and offline-only. It does not start a browser, execute JavaScript, use Chromium, or download runtime assets.

- `gtc-native-client` connects to the authoritative server and renders its replicated world in a resizable landscape window that opens at **640×480**.
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

The client uses the existing `/ws` welcome/world, state, input, path, zip, attack, toilet, customize, and cheat messages. Path messages carry sprint/advanced flags and input carries block state. It sends no positions and has no offline player-only action path. Toilet requests never auto-walk.

The default is a resizable native Win32 window on Windows or a dynamically loaded **system X11 window** on Linux. Both platforms use the same fixed-function OpenGL scene renderer: WGL on Windows and dynamically loaded GLX/OpenGL on Linux. It provides textured triangle batches, cached texture uploads, GPU depth testing, direct scene presentation, and a CPU-composited native HUD uploaded as an alpha-blended overlay texture. Vertex buffer objects are used when available, with client-array fallback. The world-space scene and its VBO contents are retained across camera frames and rebuilt only for relevant simulation/view changes; camera movement and resizing only update matrices. Three rotating textures present the CPU-composited native HUD without normal-frame scene readback. On OpenGL 2.0-capable drivers, a GLSL 1.10 material path adds tangent-space skin normal maps, two-sided diffuse/specular lighting, rim response, fog, ACES-style tone mapping, and sRGB output; unsupported or failed shader setups retain the fixed-function path. Full-frame readback happens only for an explicit F12 screenshot. The native window title reports the actual OpenGL vendor/renderer and whether GLSL or fixed-function materials are active. The interactive loop has no sleep-based frame cap and requests uncapped swaps through standard WGL/GLX extensions when available. Authoritative citizen snapshots are interpolated at 15 Hz response. Rendering, ground picking, and toilet highlighting derive the same direct camera from the current mode and local body. First person is positioned at the local citizen’s morphed, posed head/eye joints while standing, walking, and seated, with no fixed eye height or forward offset; full body geometry remains rendered. Per-character GPU model matrices move the retained meshes with that interpolated state, while gameplay remains server-authoritative. Set `GTC_GL_DEBUG=1` to enable per-frame OpenGL error checks (otherwise those synchronization-prone checks stay out of the hot path). The HUD reports `GPU` only when that context is active. If GLX/OpenGL is unavailable, Linux retains its CPU raster/`XPutImage` fallback; Windows retains its GDI fallback. Linux has no X11 or OpenGL link-time/development-package dependency and no downloaded windowing runtime. Headless captures use the CPU renderer and do not validate a GPU context. On a machine without a display, use `--headless`; useful headless options include `--frames N`, `--benchmark N`, `--width`, `--height`, `--mode tps|fps|god`, `--path X Z`, `--move X Z`, `--toilet sit|shit|piss`, `--yaw`, `--pitch`, and `--output PNG`.
A **Vulkan** backend now sits beside that OpenGL path, in `native/vulkan/`, behind the same seam: the rasterizer fills a `GpuScene`, a backend draws it, a `NativeFrame` comes back, and the window code does not care which one ran. It draws into an offscreen image with one render pass, one pipeline and one 96-byte push constant block (model matrix, colour, flags), then copies the image back into the frame the X11 path already knows how to present. It is tried first on both platforms and there is no switch that quietly puts the session on the other backend; the CPU rasterizer still backs the software path when no driver answers. Every entry point is resolved at run time from `libvulkan.so.1` through `vkGetInstanceProcAddr`, so the build has no link-time dependency on a loader. The shaders are ordinary GLSL in `native/vulkan/shaders/` (`skeleton.vert`, `skeleton.frag`), compiled with the Khronos reference compiler (`glslangValidator -V --target-env vulkan1.0`) and checked with `spirv-val`. The compiled modules and the C++ header generated from them are committed, so building the client needs no shader compiler; `python3 native/vulkan/make_shader_header.py --check` (run by `tests/run_tests.sh`, together with `spirv-val` when it is installed) fails if the header has drifted from the modules. This replaced a hand-emitted SPIR-V generator: one wrong opcode number in it produced a module that crashed Intel's shader compiler instead of being rejected. `gtc-native-smoke --vulkan-report` prints whether this machine can start the backend at all.

### Measuring a frame

Three benchmarks, each answering a different question:

    # One skull, every backend. Same triangles, same size, one after another.
    gtc-native-client --skull-benchmark 30 --renderer all --width 1568 --height 929 --root .

    # The street: the real world renderer with characters that never stop moving,
    # so nothing is a cache hit. `scene` draws no pixels and reports the CPU half.
    gtc-native-client --world-benchmark 60 --characters 3 --renderer vulkan --width 1568 --height 929 --root .

    # The test room.
    gtc-native-client --room-benchmark 60 --width 1568 --height 929 --renderer vulkan

The asset root (`--root`) is only a hint: if the skeleton is not there, the client
looks beside the executable and up from it, so a client started from `build/bin`
still finds the assets. `gtc-run.log` records which directory was used.

The skull benchmark prints the geometry cost separately from the draw, because a
backend can only own the second half.

The Vulkan figures are reported twice on purpose: **full frame** is draw, wait
for the device, and the picture copied back to host memory; **submit only** leaves
the result on the device. A backend that never copies its frame back -- OpenGL
present does not -- can only be compared against the second number. `setFrameReadback(false)`
is what the benchmark uses, and `GTC_VK_NO_COPY=1` does the same for a whole run.

Both benchmarks turn on `GTC_VK_PROFILE` themselves, so a Vulkan run prints the
frame broken into stages -- setup, record, submit and wait for the device, and the
readback -- for its first frames. When a backend is slow, that breakdown is the
only thing that says which stage to fix; `GTC_VK_PROFILE=1` does the same for a
normal run. `--renderer all` measures the software
rasterizer, Vulkan and OpenGL in one run; OpenGL needs a real window, so it is
measured through the window's own path. In the game itself, `GTC_FPS=1` reports
the smoothed frame rate once a second and the first line on stdout names the
backend that is actually carrying the frame.

The room is the acceptance measurement, and it can be timed without a display:
`gtc-native-client --room-benchmark 60 --width 1568 --height 929 --renderer vulkan`
prints ms/frame and fps for the skeleton room at that size, refusing to run if the backend cannot
start rather than quietly measuring the CPU fallback. `--renderer scene` does the same with a
backend that accepts the scene and draws nothing, which reports the half of the frame that stays on
the CPU either way; on the two-core build machine that is 4.1 ms for the room at 1568x929 (the
batches are rebuilt at the pose's 15 Hz, not per frame) against 94.6 ms for the full CPU path.
Set `GTC_FPS=1` to have the interactive loop print its own average to stderr once a second.

Native controls: **1/2/3** select TPS/god/FPS; WASD moves or fast-pans god view; the player defaults to **6.4 km/h fast walking**, **Caps Lock** toggles **4 km/h slow walking**, and **Shift** sprints at **20 km/h** (or boosts god pan); NPCs use slow walking and fast walking for urgent trips. The default citizen is **1.7 m** tall. Click captures the mouse and Esc releases it; the wheel zooms. **T** zips/unzips male trousers, which start zipped with a loose, projecting two-panel cloth tent in every view. Unzipped **LMB** (or FPS **F**) pees; **RMB** aims the ranged weapon with modest zoom or blocks with melee. In god view, hold **Alt** to crouch; **RMB** paths, **Shift-RMB** auto-runs, **double-RMB** uses advanced jump/climb/crouch traversal, **LMB** attacks, and **double-LMB** gives melee an advanced approach; ranged double-LMB simply shoots. **E** opens the in-range Sit/Shit/Piss choice or stands, and **1/2/3** pick one. **C** opens the generic style + 107-universal-morph creator, **F1** opens the need controls, and **F12** saves a native PNG. The interface on screen is one strip centred at the top with the frame rate in it, drawn by the host into the finished picture: a half-transparent panel and `<rate> FPS` in the game's own proportional sans, antialiased, at least 20 pixels high and larger on larger windows. It is not a pixel font, because this is not that kind of game. The overlay that used to be here -- corner panels, name tags, crosshair, clock, minimap, notice bar and the three pop-up panels -- was deleted rather than repaired, and is being rebuilt one piece at a time on top of that box.

The server runs a 24-hour clock advancing at **15× real time**: one game day lasts 96 real minutes. The server replicates physical piss droplets and ranged rounds with velocity, wind, 9.81 m/s² gravity, swept surface/character/toilet collision, and impact damage. A 75% bladder provides 21 seconds of continuous **9 m/s** flow; a straight-up stream clears the default citizen's head. The crosshair raycasts the replicated contact geometry and the server converges physical droplets from the emission point, including an inner-rim-clearing arc into the measured water ellipse. Urine creates supported translucent yellow-green spots on floors, walls, object sides/tops, and toilet surfaces. A 100% bladder is 600 ml (0.714286 ml per droplet); the simple 1 mm film is only a geometry conversion, giving 0.6 m² at full emptying, and radius follows square-root area. Puddles store explicit millilitres and evaporation subtracts the rounded 20 °C/50% RH flux as 200 ml/m² per game hour, instead of deleting every spot at 24 hours. The measured toilet has a 381 mm seat, 743 mm tank, and an actually open bowl with water approximately 235 mm above the floor, rather than a tall invisible catch volume.

To check the strip as pixels without a display -- centring, the half-transparent blend, the antialiased ramp of the real face, the 20-pixel floor, both byte orders and four window sizes, with a six-times enlargement left in `build/native-client/fps-counter-zoom-1920x1080.png`:

```sh
./build/bin/gtc-fps-counter-check
```

With the server running, execute the two-player test:

```sh
./build/bin/gtc-native-smoke --output build/native-smoke
```

It fails unless both clients replicate distinct identities, universal morph/style changes, server movement, a replicated path, and a server-granted seated Shit action. It writes `report.json` plus inspected **640×480 landscape** TPS, FPS, and god-view PNGs, including close toilet and zipped-cloth side views. The renderer uses the CC0 full-body mesh and rig weights, all replicated universal morphs, generic skin maps and MHCLO hair, near-plane clipping, camera obstruction handling, and authoritative puddles/actions. No portrait texture or person-specific sculpt is used.

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

The preserved browser implementation is a legacy checkpoint only. Native networking, GPU/CPU world rendering, interactive Win32/X11 window/input, the on-screen counter, and the headless multiplayer driver are implemented here. Actual native PNG review remains required for visual acceptance.

## Vendored decoder

`native/vendor/stb_image.h`, `stb_image_write.h`, and `stb_truetype.h` are from `nothings/stb`, commit `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5`. stb is dual-licensed under MIT or public domain; see `native/vendor/README.md`. These third-party headers are exempt from the authored-file line nagger. `native/assets/ui-sans.ttf` is unmodified DejaVu Sans; its Bitstream Vera/DejaVu redistribution notice is retained in `native/assets/FONT-LICENSE.txt`.
