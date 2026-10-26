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
- Single-action objects such as the throne activate directly. Multi-action objects will use a
  choice wheel rather than silently selecting an action; this slice currently has no such object.
- The throne empties both bladder and bowel and supports one occupant.
- First-person pissing empties only the bladder and leaves shared, persistent puddles.
  Aim at the toilet from close range to avoid making a floor puddle.
- **F1** opens the cheat console: boost/reduce either need by 25, empty it, or fill it. The
  server applies and clamps every change to 0–100%, so all connected views share the result.
- **H** opens the field manual and identity editor.

## One-click Windows build

Requirements: Visual Studio 2022 with **Desktop development with C++**, including the
**MSVC v143 C++ x64/x86 build tools** and a **Windows 10 or 11 SDK**.

1. Double-click **`build.cmd`**.
2. Double-click **`run.cmd`** to build (if needed) and start the game.

The script always initializes the Visual Studio environment through `vcvars64.bat` (finding
VS2022 through `vswhere` when necessary), then verifies that the C++ standard library compiles.
This matters because merely having `cl.exe` on PATH does not configure its standard headers or
libraries. Output goes to `build/bin/gtc1453.exe`; objects go to `build/obj/`.
`ccache.exe` is used automatically when available on PATH. Without it, the build still works
and prints a warning.

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

Output goes to `build/bin/gtc1453`. `ccache` is used automatically when installed.
The server binds **0.0.0.0:1453** by default. Advanced overrides:

```bash
./build/bin/gtc1453 --port 1453 --web ./web
```

## Compiler cache and offline behavior

- Persistent binaries/objects: `build/`
- Persistent ccache data: `.cache/ccache/`
- Cache configuration: `tools/ccache.conf`
- `file_stat_matches = false` and `file_stat_matches_ctime = false` make ccache hash file
  contents rather than trusting modification timestamps. `compiler_check = content` also
  hashes the compiler.
- Build products and caches remain on disk between builds but are intentionally ignored by Git.
- **No npm install, CDN, download, or network connection is needed to play.** Three.js r170 and
  its license are vendored under `web/js/vendor/`. All other art and textures are procedural.

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
validity, strict interaction range without player auto-walk, dual-need relief, clamped cheat
adjustments, pissing/puddles, and autonomous AI toilet use. It also checks camera-relative
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
build.sh      One-click Linux build
```

The simulation runs at 20 Hz on its own thread; each accepted connection has a network thread.
World mutations are mutex-protected and state snapshots are broadcast at 10 Hz. The browser
sends inputs, not positions, so collision and interaction range remain server-authoritative.

## Third party

Three.js is MIT licensed. See `web/js/vendor/THREE-LICENSE.txt` and `NOTICE.md`.
Everything else in this vertical slice is repository-local C++, HTML, CSS, JavaScript, and
procedural art.
