# OpenSkeleton — the female body after the Visible Human

The Visible Human Female is the only open *full* female skeleton that exists, and
she is a 1995 cadaver: 1 mm CT slices, 88 kg, BMI 35.7. Her thin plates and her
fingers are the parts that segment badly, which is what the debris work kept
fighting. `../docs/selecting-the-female-source.md` records why no other open
dataset can replace her.

OpenSkeleton is the first dataset that can: **51 female cadavers**, thin-slice
CT, **182 skeletal structures**, registered so that every subject carries the
same structure list.

| | |
| --- | --- |
| record | [10.5281/zenodo.22833546](https://doi.org/10.5281/zenodo.22833546) |
| licence | **CC BY 4.0** |
| subjects | 102 (51 female, 51 male), New Mexico Decedent Image Database |
| structures | 182 per subject, complete skeleton |
| creators | Duquesne K., BioCAT, Destryker T., Segers V., Audenaert E. — Ghent University |

## Why nothing is committed from it yet

The sandbox can only reach github.com, codeload, npm and PyPI. `zenodo.org`
answers `000` on connect, so the archive cannot be fetched from here. The data
must either be unblocked for `zenodo.org` (plus the storage host it redirects
to) or placed in a directory by hand.

## Files it ships

Download into one directory — `/tmp/openskeleton` is the convention used below,
because a 2.5 GB file must never land inside the repository.

| file | size | md5 | needed |
| --- | --- | --- | --- |
| `NM_combined.mat` | 2.5 GB | `0769259cb6ca6e1cf94eac9d62f64514` | **yes** — LEXT + torso + upper limbs already in one frame |
| `Faces.mat` | 7.3 MB | `2dd92c125004b59f7c0ae1e8fc5d4344` | **yes** — triangulation of every structure |
| `Metadata.mat` | 76.9 kB | `204bac57b635aed7e01447cb804f29c7` | **yes** — sex, age, stature per subject |
| `NM_LEXT.mat` + `NM_UEXT.mat` | 519 + 621 MB | see record | alternative to `NM_combined`, arms need re-registering |
| `EXAMPLE_GetData.m` | 914 B | `9c034c1639304863c65d45c926546203` | useful — the authors' own access pattern |

Direct URLs follow the form
`https://zenodo.org/api/records/22833546/files/<name>/content`.

## Layout, as confirmed by the authors' own script

`Metadata.mat` arrives first because it is 77 kB and answers everything about
who is in the set. Its checksum matches Zenodo exactly
(`204bac57b635aed7e01447cb804f29c7`), and so does `EXAMPLE_GetData.m`
(`9c034c1639304863c65d45c926546203`).

```matlab
case_selected = ['N', cases_nm{1}];
b  = NM_combined.(case_selected).('Femur_R').Registered;   % N x 3 vertices
Fb = Faces.(['F', 'Femur_R']);                             % M x 3 faces, shared
disp(Metadata.(case_selected))                             % sex, height, weight, age
```

So the triangulation is stored once per bone and shared by all 102 subjects, and
`Registered` is the corresponding shape. One subject therefore costs about 2 MB
of vertices, not 2.5 GB.

## The woman

**N186416** — female, **170.2 cm**, 78.0 kg (BMI 26.9), 41 years old. Closest of
all 51 women to the canonical 170 cm, by 2 mm. Chosen before anything was
downloaded, and she is never scaled afterwards.

| runner-up | height | why not |
| --- | --- | --- |
| N192037 | 168.0 cm | 2 cm short |
| N102924 | 172.0 cm | 2 cm tall, lean (BMI 21.1) |
| N197064 | 171.0 cm | BMI 34.9 |

## Order of work once the files are present

Only two files are needed from the 2.5 GB archive, and both are read in
streaming fashion:

```sh
pip install h5py numpy
python3 02-export-subject.py --dir /path/to/ZENODO --subject N186416 \
    --out /tmp/openskeleton/N186416.npz
```

That writes one `.npz` (a few MB) plus a JSON manifest. Upload those two; the
import step runs here, where it can be checked against the verifier.

Then, in order:

1. **Pick the subject.** A female whose CT-measured stature is closest to
   1.70 m. Her height is hers: it is never scaled to fit, exactly as with the
   two bodies already shipped.
2. **Join the frame.** `NM_combined` already carries the upper limbs in the
   torso frame; verify that by checking shoulder-to-hip distances against the
   metadata rather than trusting the field name.
3. **Map 182 structures onto the 28 rig parts** (`PART_ORDER` in
   `../02-bones.py`), keeping every named bone separate — 27 bones per hand and
   26 per foot are acceptance criteria, so grouped structures are not good
   enough.
4. **Emit the OBJ contract**: metres, soles at y = 0, facing +z, `o <part>`
   blocks, 1-based faces without `//`.
5. **Verify before shipping**: `node tools/skeleton-source/verify-obj.mjs
   <obj> --sex female --scanned`, then the shell census, then the test-room
   render at 1568 x 929, zoomed on the hands, feet and sternum.

## What it will and will not fix

It fixes the things that made her look wrong: thin plates that a 1 mm 1995 CT
cannot resolve, fingers modelled as loose slats, and a donor whose body habitus
was unlike a game character.

It does not change the pipeline's contract. The parts, the rig, the verifier and
the test room stay exactly as they are; only the geometry behind them is
replaced, and both bodies must still pass `tests/run_tests.sh` before anything
is pushed.
