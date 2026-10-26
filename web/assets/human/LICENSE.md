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

## Community hairstyles

The `afro01`, `bob01`, `braid01`, `ponytail01`, `short01`, `short02` and
`short04` sources carry the MakeHuman September 2020 CC0 dedication in their
MHCLO headers. They were also distributed in the public-domain/Unlicense
Humanizer asset collection:
<https://github.com/RavinMaddHatter/Madhatters-Table-Top-RPG-Mini-Maker>.

The `grump`, `long-bangs`, `long-waves` and `waves` sources declare **CC-BY**,
with author **Elvaerwyn**, in their embedded MHCLO headers. They were retrieved
from this asset mirror:
<https://github.com/Ismail-Bzk/Synthetic_Face_Generator/tree/main/Hair2/makehuman_system_assets_cc0/hair>.
The mirror directory name does not override those per-file license declarations.
Attribution is therefore given to Elvaerwyn for these four hairstyles and their
derived runtime meshes/textures. The `wavy-bob`, `curly-bob` and `fifties-updo`
sources also declare **CC-BY** and author **Elvaerwyn** in their MHCLO headers;
they came from the mirror's `Hair2/hair02_ccby/hair` directory. The same
attribution and license notices apply to their derived meshes and textures.
Changes to all seven Elvaerwyn sets include MHCLO fitting/packing, UV-seam
preservation, skin-weight interpolation, and grayscale/alpha texture conversion.
Source: <https://github.com/Ismail-Bzk/Synthetic_Face_Generator>.

`tools/convert-hair.py` reproduces the generated runtime geometry packs offline:

```bash
python3 tools/convert-hair.py male
python3 tools/convert-hair.py female
```

## Portrait fitting references

The seven reference portraits and their landmark measurements are development
inputs under `tools/human-source/portrait-fitting/`. They are used only to fit
and evaluate geometry sliders and numeric sculpt targets. They are not browser
assets and no portrait photograph, projected photograph, or photo-derived
facial texture is included in the runtime. Their presence in the development
inputs is not a CC0 dedication or a grant of additional rights.

`tools/build-sculpts.py` fits the custom `.target` files under
`tools/human-source/targets/custom/`. Those files contain only MakeHuman vertex
indices and numeric geometry deltas: no pixels, image UVs, or encoded portraits.
The opposite-sex placeholder targets are intentionally empty.
