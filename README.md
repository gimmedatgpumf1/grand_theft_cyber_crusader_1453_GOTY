# GRAND THEFT CYBER CRUSADER 1453 — GOTY

### Porcelain Protocol: the first working toilet

A goofy, low-poly, server-authoritative bathroom sim. One city sector, one toilet,
questionable citizens, and biology with a network tick rate.

This repository currently implements the **first playable vertical slice**, not the whole
kitchen-sink game concept. It includes the foundations needed by that concept: C++ server,
offline browser UI, multiplayer, shared AI/player actions, collision, pathfinding, needs,
object interaction, and three camera modes.

## Play

Open **http://localhost:1453** after starting the server. Open the same address in another
browser window or on another machine on the LAN to join as a different citizen. There is no
local-player-only action path: the AI uses the same movement, pathfinding, pissing, and toilet
actions as browser players.

### Controls

| Mode | Move citizen | Camera | Interact |
| --- | --- | --- | --- |
| **1 — Third person** | WASD; Shift to sprint | Click view for mouse orbit; Esc releases; wheel zoom | E or centered left-click in range |
| **2 — Godview** | Left-click floor to pathfind | WASD fast-pans; Shift boosts; wheel zoom | Left-click a highlighted object in range; E also works |
| **3 — First person** | WASD; Shift to sprint | Click view to capture mouse; Esc releases | E in range; **hold F to piss** |

- Targeted objects glow. **[E] Interact** appears only inside the **1.8 m interaction range**.
- E and object clicks never auto-walk the player. Move closer yourself; explicit godview floor
  clicks still use pathfinding. Autonomous AI can path to the toilet before using it.
- The throne is multi-use: E opens a real choice wheel for **Sit**, **Shit**, and **Piss**.
  Sit persists until E or WASD stands up. Shit drains only bowel; Piss drains only bladder.
  Males piss standing at the bowl; females use the seated pose. Toilet piss makes no floor puddle.
- **C** opens the character creator: sex, skin, hair, colors, facial hair, coat and sunglasses
  are server-validated and replicated. JC, field-agent and classic presets are included.
- First-person view renders the local torso, arms, hands, legs and boots when looking down.
  Holding F outside a toilet action empties only the bladder and leaves shared, persistent puddles.
- **F1** opens the cheat console: boost/reduce either need by 25, empty it, or fill it. The
  server applies and clamps every change to 0–100%, so all connected views share the result.
- **H** opens the field manual and identity editor.

## One-click Windows build

Requirements: Visual Studio 2022 with **Desktop development with C++**, including the
**MSVC v143 C++ x64/x86 build tools** and a **Windows 10 or 11 SDK**.

1. Double-click **`build.cmd`** (or run **`build.ps1`**).
2. Double-click **`run.cmd`** to build (if needed) and start the game.

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
./build.sh          # build
./run.sh            # build and run
# or: ./build.sh --run
```

Output goes to `build/bin/gtc1453`, with a committed copy at `prebuilt/linux-x86_64/gtc1453`.
On Linux x86-64 you can run that prebuilt binary directly without compiling. The build uses the
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
- **No npm install, CDN, download, or network connection is needed to build or play.** Three.js
  r170 and its license are vendored under `web/js/vendor/`. Art and textures are procedural.

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

The test runner builds and executes simulation tests for JSON, collision/sliding, A* path
validity, strict interaction range without player auto-walk, persistent sitting, separate
bowel/bladder relief, standing male and seated female toilet piss, appearance validation,
clamped cheat adjustments, puddles, and autonomous AI toilet use. It also checks camera-relative
strafe directions, the input protocol's world-space x/z mapping, mouse-look direction,
godview pan speed, and syntax-checks every first-party
browser module with Node.js. Node is only a test convenience, not a game dependency.

## Architecture

```text
src/
  core/       World state, needs, collision, A*, JSON protocol
  net/        POSIX/Winsock abstraction, SHA-1 handshake, WebSocket frames
  server/     HTTP static files + WebSocket transport + threaded simulation
  main.cpp    Port/web-root options
web/
  index.html  HUD and field manual
  styles.css  Acid-green CRT-ish interface
  js/         Renderer, cameras, controls, networking, characters, HUD
  js/vendor/  Offline Three.js
build.cmd     VS2022 auto-detection and one-click MSVC build
build.ps1     PowerShell entry point for the same Windows build
build.sh      One-click cached Linux build
prebuilt/     Committed Linux x86-64 server binary
cache/        Portable ccache seed
tools/ccache/ Vendored ccache, licenses and corresponding sources
```

The simulation runs at 20 Hz on its own thread; each accepted connection has a network thread.
World mutations are mutex-protected and state snapshots are broadcast at 10 Hz. The browser
sends inputs, not positions, so collision and interaction range remain server-authoritative.

## Third party

Three.js is MIT licensed. See `web/js/vendor/THREE-LICENSE.txt` and `NOTICE.md`.
The optional build accelerator is ccache 4.14.2, GPL-3.0-or-later; its license and complete
corresponding source archives are under `tools/ccache/`. The game itself is repository-local
C++, HTML, CSS, JavaScript, and procedural art.
