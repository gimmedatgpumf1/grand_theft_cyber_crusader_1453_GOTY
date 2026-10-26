# Character asset provenance

The runtime character packs are generated from **MakeHuman CC0 1.0 Universal assets**.
No MakeHuman or MPFB program code is included.

## MakeHuman

Upstream: <https://github.com/makehumancommunity/makehuman>
Reviewed commit: `a8bc2d54ff0ac92e78ff71431b1023eda42bf482`.

Vendored source inputs under `tools/human-source/`:

- `base.obj`: hm08 body topology, UVs and helper joints.
- `default.mhskel`: default skeleton.
- `default_weights.mhw`: symmetric body weights.
- `caucasian-male-young.target` and `caucasian-female-young.target`: macro shape targets.

The asset headers identify the September 2020 CC0 release. Named upstream asset
copyright holders include Data Collection AB, Joel Palmius, Jonas Hauquier and
Manuel Bastioni; see the individual source headers. The full CC0 text is in `CC0.txt`.

## MPFB detail masks

Upstream: <https://github.com/makehumancommunity/mpfb2>
Reviewed commit: `6ff1eb358f6c1959ced77e79e6dd2406d9a71f49`.

`lips.png` and `eyelids.png` are alpha-mask conversions of MPFB's CC0
`mpfb_lips.jpg` and `mpfb_eyelids.jpg`. MPFB explicitly releases bundled textures
under CC0; its GPL applies to program code, which is not used here.

## Generated/runtime files

`web/js/vendor/human-male-data.js` and `human-female-data.js` contain compact
positions, UVs, reduced 33-bone rigs, normalized weights, material groups, hair
indices and facial landmarks. `tools/convert-human.py` reproduces them offline
from the vendored inputs using only the Python standard library:

```bash
python3 tools/convert-human.py male
python3 tools/convert-human.py female
```

Skin colors, facial color compositing, fabric, clothing and accessories are
created locally by the game. All required runtime assets are served locally;
nothing is downloaded at runtime.
