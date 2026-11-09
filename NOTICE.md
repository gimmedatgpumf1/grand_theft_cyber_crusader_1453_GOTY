# Third-party notices

## Three.js r170 (0.170.0)

- Project: https://threejs.org/
- Source package: https://www.npmjs.com/package/three/v/0.170.0
- License: MIT
- Included file: `web/js/vendor/three.module.min.js`
- Full license: `web/js/vendor/THREE-LICENSE.txt`

Three.js is vendored so the game works fully offline. No remote runtime assets are used.

## MakeHuman / MPFB character assets (CC0 1.0)

- Projects: https://github.com/makehumancommunity/makehuman and
  https://github.com/makehumancommunity/mpfb2
- License: CC0 1.0 Universal for the bundled graphical assets; no project program code is included.
- Source geometry, rigs, weights and shape targets: `tools/human-source/`
- Generated local runtime packs: `web/js/vendor/human-*-data.js`
- Local facial masks, provenance and full license: `web/assets/human/`
- Offline generator: `tools/convert-human.py`

## ccache 4.14.2 (optional build accelerator)

- Project and source: https://github.com/ccache/ccache
- License: GPL-3.0-or-later; bundled dependencies retain their own notices.
- Included executable: `tools/ccache/linux-x86_64/ccache`
- Licenses and attribution: `tools/ccache/LICENSE.adoc`, `GPL-3.0.txt`, `LGPL-3.0.txt`
- Complete corresponding source archives: `tools/ccache/source/`
- Integrity hashes: `tools/ccache/SHA256SUMS`

Ccache is a build-time tool, not a game runtime dependency. Its committed sources permit an
offline audit/rebuild as described in `tools/ccache/BUILDING.md`.
