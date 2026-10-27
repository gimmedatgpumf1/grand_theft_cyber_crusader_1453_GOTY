# Rebuilding the vendored Linux ccache

Normal players and developers do **not** need CMake, Ninja, or these steps. `./build.sh` uses the committed executable directly.

To audit or reproduce the tool, install a C/C++ compiler, CMake and Ninja. Then run the following from the repository's `tools/ccache` directory:

```sh
TOOLS="$PWD"
WORK="$(mktemp -d)"
cd "$WORK"
mkdir ccache zstd xxhash build
tar -xzf "$TOOLS/source/ccache-4.14.2.tar.gz" -C ccache --strip-components=1
tar -xzf "$TOOLS/source/zstd-1.5.7.tar.gz" -C zstd --strip-components=1
tar -xzf "$TOOLS/source/xxhash-0.8.3.tar.gz" -C xxhash --strip-components=1
cmake -S ccache -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSTATIC_LINK=ON \
  -DENABLE_TESTING=OFF \
  -DENABLE_DOCUMENTATION=OFF \
  -DREDIS_STORAGE_BACKEND=OFF \
  -DHTTP_STORAGE_BACKEND=OFF \
  -DFETCHCONTENT_SOURCE_DIR_ZSTD="$WORK/zstd" \
  -DFETCHCONTENT_SOURCE_DIR_XXHASH="$WORK/xxhash"
cmake --build build --parallel
cp build/ccache "$TOOLS/linux-x86_64/ccache"
```

The `FETCHCONTENT_SOURCE_DIR_*` options are important: they make the rebuild use only the committed archives and prohibit a dependency download. Check the result with `$TOOLS/linux-x86_64/ccache --version` and `ldd $TOOLS/linux-x86_64/ccache`.
