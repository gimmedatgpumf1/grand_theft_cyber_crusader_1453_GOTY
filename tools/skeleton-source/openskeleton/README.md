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

## Included subject

`data/skelly-out.npz` and its manifest contain the 182 exported registered
structures for subject N186416. `data/male-N197925.npz` and its manifest contain
the same source structures for subject N197925. The original multi-subject archive is not in
this repository. `03-import.py` uses the included files directly (default female; `--male` for
male), preserves
its per-bone registered placement and source names, and simplifies each mesh.
The result is not guaranteed to be the original CT patient pose or an
anatomically assembled skeleton; source registration leaves visible asymmetries.

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

## Rebuilding and checking the included subject

```sh
python3 tools/skeleton-source/openskeleton/03-import.py
python3 tests/openskeleton_source_pose.py
```

The importer needs NumPy and `fast-simplification`. It applies only a common
coordinate transform and translation, keeps the source left and right bones,
and retains each source name in the OBJ and manifest. The test checks those names,
rig group mappings, faces and proximity of every simplified vertex to its own
source bone. It cannot establish anatomical correctness of the source labels
or the source registration.

## Extracting a male subject on Windows

The current male test candidate is **N197925**, whose metadata records a
170 cm cadaver length. Put `NM_combined.mat` and `Faces.mat` in `E:\zenodo`.
From the repository root, run:

```cmd
py -3 tools\skeleton-source\openskeleton\02-export-subject.py --dir E:\zenodo --subject N197925 --out E:\zenodo\male-N197925.npz
```

Python 3 with NumPy, SciPy and h5py is required. Send the resulting `.npz`
and `.json`; the source geometry is not rescaled in this step.

The exporter has been checked using the real Faces.mat and a synthetic MATLAB
v5 file containing 182 structures. The full 2.5 GB combined file has not been
tested in this environment. If the combined file is not HDF5, SciPy loads it
into memory and may need several gigabytes of RAM.
