# Vendored human source assets

These MakeHuman inputs are the offline sources for the two runtime packs in
`web/js/vendor/`. They are graphical assets released under CC0 1.0, not MakeHuman
program source. Upstream trailing whitespace is normalized; semantic data is unchanged
and regeneration is byte-identical. See `web/assets/human/LICENSE.md` for upstream
URLs, reviewed commits and provenance, and `CC0.txt` for the license text.

Regenerate from the repository root:

```bash
python3 tools/convert-human.py male
python3 tools/convert-human.py female
```
