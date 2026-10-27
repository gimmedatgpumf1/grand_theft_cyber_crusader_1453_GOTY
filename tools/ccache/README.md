# Vendored ccache

`linux-x86_64/ccache` is ccache **4.14.2**, built from the official source in this directory. It is used automatically by `build.sh`; no runtime download or package installation is needed. The executable was linked with static libstdc++, libgcc, zstd, xxhash, BLAKE3 and fmt, and requires only the normal Linux C runtime and libm.

`build.cmd` prefers `windows-x86_64/ccache.exe` when present, then a system `ccache.exe`, and otherwise compiles directly with MSVC. A Windows ccache executable is not fabricated or claimed in this checkout; the normal Windows build does not depend on it.

The corresponding ccache 4.14.2, zstd 1.5.7 and xxHash 0.8.3 source archives are committed under `source/` so the binary can be audited and rebuilt offline. See `BUILDING.md`. Upstream project: <https://github.com/ccache/ccache>. Runtime use does not fetch anything.

The cache format is local compiler-cache data, not a runtime dependency of the game. `cache/ccache-linux-x86_64.tar.gz` is restored only when `.cache/ccache` is empty. Existing developer caches are never overwritten.
