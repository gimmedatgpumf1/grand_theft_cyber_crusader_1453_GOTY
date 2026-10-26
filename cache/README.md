# Portable compiler-cache seed

`ccache-linux-x86_64.tar.gz` contains only ccache result/manifest entries for the game's nine C++ compilation units. Statistics, locks, inode caches and temporary files are excluded. The archive is roughly 170 KB.

On Linux, `build.sh` restores it only when `.cache/ccache` is empty. Relative `src/...` compilation paths, `compiler_check=content` and `hash_dir=false` allow direct hits from different checkout directories with the same compiler and sources. It was verified from two different paths with **9/9 direct hits and zero misses** after restoration.

After changing C++ sources, rebuild the binary and refresh the seed explicitly:

```sh
./tools/rebuild-cache-seed.sh
```

That command deliberately clears the local cache, performs a cold build, writes a clean seed and updates `SHA256SUMS`. Normal builds never clear the cache. Verify the archive with:

```sh
(cd cache && sha256sum -c SHA256SUMS)
```
