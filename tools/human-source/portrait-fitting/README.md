# Portrait fitting workbench

This directory preserves the development inputs for the seven portrait presets.
It is not used by the game server or browser runtime.

- `reference-landmarks.json` contains the saved 468-point facial measurements and
  source image dimensions.
- `canonical_face_model.obj` is the MediaPipe canonical face mesh used by the
  fitter and landmark-surface generator.
- `current-presets.json` preserves the preset body/heritage controls excluded
  from facial optimization.
- `fitted-presets.json` is the latest fit result, including camera parameters.
- `references/` contains the original user-supplied portraits.

From the repository root, with NumPy, SciPy and OpenCV available to Python:

```bash
python3 tools/fit-portraits.py
python3 tools/update-portrait-presets.py
```

The first command rewrites only the fitted slider results and ignored
`build/fit-*.png` landmark diagnostics. It does not create character textures. The second merges facial morphs into
`web/js/appearance.js` without discarding the excluded controls. No network
access is needed when using these saved inputs.

The reference portraits were supplied for this character-recreation task. Their
presence here is not a CC0 dedication or a grant of additional rights.

The browser never loads these reference portraits or derives likeness textures
from them. Only general-purpose morph values are merged into the runtime presets.
