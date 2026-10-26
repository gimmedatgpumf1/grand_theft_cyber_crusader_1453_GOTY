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
