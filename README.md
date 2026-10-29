# GRAND THEFT CYBER CRUSADER 1453 — GOTY

### Porcelain Protocol: the first working toilet

A goofy, low-poly, server-authoritative bathroom sim. One city sector, one toilet,
questionable citizens, and biology with a network tick rate.

This repository currently implements the **first playable vertical slice**, not the whole
kitchen-sink game concept. It includes a C++ authoritative server, a native C++ software-rendered
client, multiplayer, shared AI/player actions, collision, pathfinding, needs, object interaction,
and three camera modes. The old browser UI is retained only as a legacy checkpoint.

## Play

On Windows, **double-click `run.cmd`** to build both targets, wait for port 1453, and open the
native GUI client automatically. On Linux, `./run.sh` does the same. The manual alternative is:

```bash
./build.sh
./native/build.sh
./build/bin/gtc1453 --port 1453 --web web
# In another terminal:
./build/bin/gtc-native-client --name "Native Citizen"
```

On Windows use `build.cmd` and `native\\build.cmd`, then run `build\\bin\\gtc1453.exe` and
`build\\bin\\gtc-native-client.exe`. Separate native clients on the LAN join as different citizens.
There is no local-player-only action path: AI and players share movement, pathfinding, pissing,
and toilet actions. The client sends inputs, never authoritative positions. Use `--headless` for
native PNG output without a display. See [`native/README.md`](native/README.md) for all options.

### Controls

| Mode | Move citizen | Camera | Interact |
| --- | --- | --- | --- |
| **1 — Third person** | WASD; Shift to sprint | Click view for mouse orbit; Esc releases; wheel zoom | E; **T** toggles a male zip; unzipped LMB pees; RMB aims/blocks |
| **2 — Godview** | RMB paths; Shift-RMB auto-runs; double-RMB uses advanced traversal; hold Alt to crouch | WASD fast-pans; Shift boosts; wheel zoom | LMB attacks; double-LMB melee uses advanced traversal; E interacts |
| **3 — First person** | WASD; Shift to sprint | Click view to capture mouse; Esc releases | E; **T** toggles a male zip; unzipped LMB/F pees; RMB aims/blocks |

- Targeted objects glow, and **E** interacts only inside the **1.8 m interaction range**.
- E never auto-walks the player. Move closer yourself; explicit godview RMB paths use ordinary A*.
  Double-RMB instead permits server-controlled jumping, climbing, crouching, and crawling en route.
  Autonomous AI can path to the toilet before using it.
- The throne is multi-use: E opens an interaction circle -- one disc divided into three equal
  sectors, **SIT**, **SHIT** and **PISS**, with a cancel button where they meet. Move the pointer and
  the sector under it lights up; click it, or press **1/2/3**. Clicking the middle cancels and closes
  it, and clicking outside the disc does nothing. The disc and the click answer to the same geometry,
  so a sector cannot be one place on the screen and another place under the mouse.
  Sit persists until E or WASD stands up. Shit drains only bowel; Piss drains only bladder.
  Males piss standing at the bowl; females use the seated pose. Toilet piss makes no floor puddle.
- **C** opens/closes the skeleton test room. The room has an independent free camera:
  move with **WASD**, look with the mouse, fly up/down with **Space/Left Ctrl**, hold **Shift**
  for faster movement, and use the wheel to move closer or farther. **Esc** releases the
  mouse; click to capture it again. Room controls do not move the player in the game.
- First-person view renders the local torso, arms, hands, legs and boots when looking down.
  Male anatomy starts zipped behind a loose, projecting cloth tent in FPS and TPS; **T** intentionally
  exposes it. Holding LMB (or F in FPS) outside a toilet action works only while unzipped and empties
  only the bladder. A bladder at 75% supports **21 seconds** of continuous flow at **9 m/s**, sufficient
  to clear the citizen's head when fired straight up. Close-range fire converges from the emission point
  onto the crosshair's contacted surface; toilet shots clear the inner rim and land in the real water ellipse.
  Piss droplets and unzipped ranged rounds are server-simulated physical objects under
  **9.81 m/s² gravity and wind**, with swept collisions rather than decorative trajectory curves.
  Ranged attacks are always attempted; damage occurs on impact.
- The toilet follows Kohler Wellworth K-3987 measurements: a **381 mm seat**, **743 mm tank**,
  **705 mm depth**, and separate tank, rim, open bowl, water, and pedestal collision. Small translucent
  yellow-green stains attach to floors, object tops, sides, walls, toilet surfaces, and water; overlapping
  floor spots spread sideways. One game day is **96 real minutes (15× time)**, shown by the native clock,
  and urine stains dry after one complete game day. God LMB uses melee while zipped and the physical
  ranged weapon otherwise; RMB blocks melee or applies explicit ranged aim zoom.
- **F1** opens the need controls: boost/reduce either need by 25, empty it, or fill it, with the
  current percentages on the panel. The server applies and clamps every change to 0–100%, so all
  connected views share the result. Every entry is a button when the mouse is free, and so is CLOSE.
- **H** opens the controls sheet, two columns so it fits a 640×480 frame at the 20-pixel text floor.
- **F12** saves the current native **640×480 landscape** framebuffer.
- Pointing at something you can use says so: the object lights up in the world (accent pool and ring
  under it, unlit so it reads on any floor) and two lines of plain text appear under the reticle,
  **[E] Interact** then the name of the thing, with no panel around them. Nothing is shown when
  nothing is pointed at, and `E` fires on exactly the same condition the highlight and the words
  draw on -- being in range is not enough, you have to be pointing at it.
- Five things are drawn over the picture, and they are being rebuilt one at a time: the reticle
  (centred, dark-outlined, absent in god view), the **F1** need panel (translucent, centred, two
  columns of keys and their values), the mode bar along the bottom (**1 3rd person / 2 god /
  3 1st person**, the running one ruled underneath), the needs panel in the bottom right
  (**PISS**, **SHIT**, **HP** as three small bars with their values) and the frame-rate strip at the top. The strip
  is the faintest of them at half strength; the panel is darker because it has to be read. Text is
  the game's own proportional sans, antialiased, 20 pixels minimum at 640×480 and larger as the
  window grows. The old overlay is gone, not hidden.

## One-click Windows build

Requirements: Visual Studio 2022 with **Desktop development with C++**, including the
**MSVC v143 C++ x64/x86 build tools** and a **Windows 10 or 11 SDK**.

1. Double-click **`build.cmd`** (or run **`build.ps1`**) for the authoritative server.
2. Double-click **`native\\build.cmd`** (or **`native\\build.ps1`**) for the native client,
   smoke driver, and fitter.
3. Or skip these manual steps and double-click **`run.cmd`**; it performs both builds, starts the
   server, waits for the port, and opens `gtc-native-client.exe` automatically.

The script always initializes the Visual Studio environment through `vcvars64.bat` (finding
VS2022 through `vswhere` when necessary), then verifies that the C++ standard library compiles.
This matters because merely having `cl.exe` on PATH does not configure its standard headers or
libraries. Output goes to `build/bin/gtc1453.exe`; objects go to `build/obj/`.
Each source is compiled to its own object so ccache can cache real compilation units.
`ccache.exe` is preferred at `tools/ccache/windows-x86_64/ccache.exe` when supplied, then on PATH.
Without it, the build still works and prints a warning; no Windows ccache binary is claimed here.

If the preflight reports that `cmath` or the C++ standard library is unavailable, open
**Visual Studio Installer → Modify → Desktop development with C++** and enable the two
components listed above. No repository headers named `cmath`, `map`, or `string` are needed;
those are supplied by MSVC.

## One-click Linux build

Requirements: a C++17 compiler (`g++` preferred, `clang++` fallback) and Bash.

```bash
./build.sh          # authoritative server
./native/build.sh   # native client, smoke driver, and fitter
./run.sh            # build and run the server
# or: ./build.sh --run
```

Server output goes to `build/bin/gtc1453`; native outputs are `gtc-native-client`,
`gtc-native-smoke`, and `gtc-native-fit`. Committed Linux x86-64 copies and checksums live under
`prebuilt/linux-x86_64/`. The build uses the
vendored ccache 4.14.2 automatically, with system ccache and direct compilation as fallbacks.
The server binds **0.0.0.0:1453** by default. Advanced overrides:

```bash
./build/bin/gtc1453 --port 1453 --web ./web
```

## Compiler cache and offline behavior

- Persistent local binaries/objects: `build/`; committed Linux binary: `prebuilt/linux-x86_64/`.
- Persistent local ccache data: `.cache/ccache/`; committed seed: `cache/ccache-linux-x86_64.tar.gz`.
- An empty local cache automatically restores the seed. Existing caches are never overwritten.
  After C++ changes, `tools/rebuild-cache-seed.sh` explicitly refreshes the binary and seed.
- Vendored ccache, licenses, checksums and corresponding offline source archives: `tools/ccache/`.
- Cache configuration: `tools/ccache.conf`. Builds compile nine separate objects before linking.
- `file_stat_matches = false` and `file_stat_matches_ctime = false` make ccache hash file
  contents rather than trusting modification timestamps. `compiler_check = content` also
  hashes the compiler; `hash_dir = false` permits reuse from another checkout path.
- Local build products and cache state remain ignored by Git. The prebuilt binary, ccache tool,
  source archives and compressed seed are deliberately committed for GitHub-hosted acceleration.
- **No npm install, CDN, or runtime download is needed.** The native client uses Win32 or the
  system X11 library and local assets. The CC0 MakeHuman geometry, rigs, shape targets, universal
  skin maps and MPFB detail masks are vendored; see `web/assets/human/LICENSE.md` for asset
  provenance. Clothing, fabric, skin-color compositing and the remaining art are procedural.
  Three.js under `web/js/vendor/` belongs only to the preserved legacy checkpoint.

## The 1,000-line nagger

Every build runs `tools/nagger.sh` or `tools/nagger.ps1`. A first-party source/script/UI file
over 1,000 lines fails the check and demands to be split. Vendored libraries, `.git`, build
products, and caches are excluded. Run it directly on Linux:

```bash
bash tools/nagger.sh
```

## Tests

```bash
./tests/run_tests.sh
```

The test runner builds and executes C++ simulation tests for JSON, collision/sliding, A* path
validity, strict interaction range without player auto-walk, persistent sitting, separate
bowel/bladder relief, standing male and seated female toilet piss, appearance validation,
clamped cheat adjustments, puddles, and autonomous AI toilet use. When the native binaries are
built, it also starts a separate test server and runs two real native clients through replication,
customization, movement, pathfinding, and toilet interaction. It does not invoke browser tooling.

The native build also produces `build/bin/gtc-vulkan-ui-check` (`.exe` on Windows).
Run it from the repository root to render the interface offscreen with the installed Vulkan
implementation and check its pixels, alpha, font coverage, placement and resize behaviour.
This is not an interactive presentation or frame-rate test. On Linux it can also be included
in the test suite with `GTC_VULKAN_UI_TEST=1 ./tests/run_tests.sh`.

Interactive frames draw the UI on Vulkan; the font-coverage asset is generated offline from
`native/assets/ui-sans.ttf` with `tools/bake-ui-atlas.cpp`. The runtime lays out text but does
not rasterize glyphs on the CPU. F12 is an explicit screenshot readback, not the normal frame path.
Vulkan keeps unchanged geometry batches resident and uploads the changed suffix; a growing buffer
or changed world revision can require a full upload. Presentation uses per-frame uniform/command
slots, but geometry buffers are shared and updates still wait for earlier GPU use. The offscreen
check compares incremental and forced full uploads; it does not validate Windows presentation or FPS.

## Native C++ runtime and portrait fitter

The native-only path is in [`native/`](native/README.md). Build `./native/build.sh` (or
`native\\build.cmd` with Visual Studio 2022). `gtc-native-client` is the interactive/headless
multiplayer client; `gtc-native-smoke` is the two-player network test. For portrait work,
`./build/bin/gtc-native-fit --iterations 8` performs direct numeric universal-morph/camera fitting
and writes separate **480×640**
CPU-rasterized initial/final PNGs for visual review. It does not use a browser, JavaScript,
Chromium, portrait textures, or per-candidate image detection. Native presets live in
`native/presets.json`; candidates are never promoted automatically.

## Architecture

```text
src/
  core/       World state, needs, collision, A*, JSON protocol
  net/        POSIX/Winsock abstraction, SHA-1 handshake, WebSocket frames
  server/     WebSocket transport + threaded authoritative simulation
  main.cpp    Port/web-root options
native/
  client/     Protocol, software world renderer, Win32/X11 window, input, native UI
  vendor/     stb image decoding/encoding
web/          Legacy browser checkpoint and reusable local generic asset files
tools/convert-human.py Offline human-pack generator
tools/human-source/ CC0 MakeHuman source geometry, targets and weights
build.cmd     VS2022 auto-detection and one-click MSVC build
build.ps1     PowerShell entry point for the same Windows build
build.sh      One-click cached Linux build
prebuilt/     Committed Linux x86-64 server binary
cache/        Portable ccache seed
tools/ccache/ Vendored ccache, licenses and corresponding sources
```

The simulation runs at 20 Hz on its own thread; each accepted connection has a network thread.
World mutations are mutex-protected and state snapshots are broadcast at 10 Hz. Native clients
send inputs, not positions, so collision and interaction range remain server-authoritative.

## Third party

Three.js is MIT licensed. See `web/js/vendor/THREE-LICENSE.txt` and `NOTICE.md`.
The optional build accelerator is ccache 4.14.2, GPL-3.0-or-later; its license and complete
corresponding source archives are under `tools/ccache/`. Character geometry, rigs, targets and
facial masks are CC0 MakeHuman/MPFB assets; provenance and regeneration instructions are in
`web/assets/human/LICENSE.md`. No MakeHuman/MPFB program code is distributed. The active game
runtime is repository-local C++; legacy HTML/CSS/JavaScript is not the active client.

The UI font and its derived glyph-coverage asset are covered by `native/assets/FONT-LICENSE.txt`.
