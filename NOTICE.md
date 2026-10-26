# Third-party notices

## Three.js r170 (0.170.0)

- Project: https://threejs.org/
- Source package: https://www.npmjs.com/package/three/v/0.170.0
- License: MIT
- Included file: `web/js/vendor/three.module.min.js`
- Full license: `web/js/vendor/THREE-LICENSE.txt`

Three.js is vendored so the game works fully offline. No other third-party runtime libraries
or remote assets are used.

## ccache 4.14.2 (optional build accelerator)

- Project and source: https://github.com/ccache/ccache
- License: GPL-3.0-or-later; bundled dependencies retain their own notices.
- Included executable: `tools/ccache/linux-x86_64/ccache`
- Licenses and attribution: `tools/ccache/LICENSE.adoc`, `GPL-3.0.txt`, `LGPL-3.0.txt`
- Complete corresponding source archives: `tools/ccache/source/`
- Integrity hashes: `tools/ccache/SHA256SUMS`

Ccache is a build-time tool, not a game runtime dependency. Its committed sources permit an
offline audit/rebuild as described in `tools/ccache/BUILDING.md`.
