# Native C++ runtime and portrait fitter

The native path is C++17 and offline-only. It does not start a browser, execute JavaScript, use Chromium, or download runtime assets.

- `gtc-native-client` connects to the authoritative server and renders its replicated world at **480×640**.
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

Start the existing authoritative server from the repository root, then connect a native client:

```sh
./build/bin/gtc1453 --port 1453 --web web
./build/bin/gtc-native-client --name "Native Citizen" --frames 1 --output build/native-client/frame.png
```

The client uses the existing `/ws` welcome/world, state, input, path, toilet, customize, and cheat messages. It sends no positions and has no offline player-only action path. Useful headless options include `--mode tps|fps|god`, `--path X Z`, `--move X Z`, `--toilet sit|shit|piss`, `--yaw`, and `--pitch`. Toilet requests never auto-walk.

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

The preserved browser implementation is a legacy checkpoint only. Native networking, rendering, and the headless multiplayer driver are implemented here; the native interactive window/input layer is the next runtime milestone.

## Vendored decoder

`native/vendor/stb_image.h` and `stb_image_write.h` are from `nothings/stb`, commit `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5`. stb is dual-licensed under MIT or public domain; see `native/vendor/README.md`. These third-party headers are exempt from the authored-file line nagger.
