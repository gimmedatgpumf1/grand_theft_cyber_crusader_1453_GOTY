# Prebuilt server

`linux-x86_64/gtc1453` is the current C++17 server binary for Linux x86-64 with glibc. It serves the browser UI and WebSocket multiplayer on port 1453.

```sh
./prebuilt/linux-x86_64/gtc1453
# Optional: ./prebuilt/linux-x86_64/gtc1453 --port 1453 --web ./web
```

Run it from the repository root so the default `web` path resolves. `build.sh` refreshes this copy after each Linux x86-64 build and updates `SHA256SUMS`. Verify with:

```sh
(cd prebuilt && sha256sum -c SHA256SUMS)
```

Windows builds remain a local MSVC compile through `build.cmd`/`build.ps1`; no untested Windows game executable is claimed.

## Native fitter

`linux-x86_64/gtc-native-fit` is the offline C++17 480×640 portrait fitter/software renderer.
It does not serve the game or require a browser. Run it from the repository root, for example:

```sh
./prebuilt/linux-x86_64/gtc-native-fit --iterations 8
```

Verify it with `(cd prebuilt && sha256sum -c NATIVE_SHA256SUMS)`. `native/build.sh` refreshes
this copy; `native/build.cmd` builds the corresponding local MSVC executable.
