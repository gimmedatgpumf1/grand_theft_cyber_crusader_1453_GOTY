#!/usr/bin/env python3
"""Apply King's head-height formulas to the existing whole-body TSVs.

Recover the per-row circumference and IDs from the pinned 21-column export,
then verify each against its same-subject source record before conversion.
All other whole-body columns are left unchanged. See WHOLE_BODY_HEAD_DATA.md.
"""
import argparse
import csv
from decimal import Decimal
import io
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / 'tools/skeleton-source/anthropometry'
HISTORICAL = '38935c141595338ae59b2407dd89c9d1b1546083'
HEADER = ['height_cm', 'head_proxy_cm', 'neck_cm', 'torso_cm', 'arm_cm',
          'leg_cm', 'pelvis_cm', 'shoulder_cm']
PAIRS = {
    'neck_cm': ('neck_from_landmarks_cm', 'neck_proxy_from_head_circumference_cm'),
    'torso_cm': ('torso_from_landmarks_cm', 'torso_proxy_from_sitting_height_cm'),
    'arm_cm': ('arm_from_landmarks_cm', 'arm_proxy_from_upper_arm_cm'),
    'leg_cm': ('leg_trochanter_height_cm', 'leg_proxy_from_subischial_height_cm'),
    'pelvis_cm': ('pelvis_bicristal_cm',),
    'shoulder_cm': ('shoulder_biacromial_cm',),
}
FORMULAS = {'MALE': (Decimal('0.5119'), Decimal('-7.4526')),
            'FEMALE': (Decimal('0.4539'), Decimal('-4.5274'))}
SOURCES = {
    'NHANES_III_CHILD': ('NHANES_III_PAIRED_CM.csv', 'SEQN', 'BMPHEAD', 'BMPHT'),
    'NHANES_III_INFANT': ('NHANES_III_INFANT_PAIRED_CM.csv', 'SEQN', 'BMPHEAD', 'BMPRECUM'),
    'ANSUR_II': ('ANSUR_II_PAIRED_MM.csv', 'SubjectId', 'headcircumference', 'stature'),
}


def head_estimate(sex, circumference_cm):
    slope, intercept = FORMULAS[sex]
    return slope * Decimal(circumference_cm) + intercept


def inputs():
    records = {}
    for name, (filename, idcol, _, _) in SOURCES.items():
        with (DATA / filename).open(newline='', encoding='utf-8') as f:
            records[name] = {r[idcol]: r for r in csv.DictReader(f)}
    return records


def historical_rows(sex):
    path = f'tools/skeleton-source/anthropometry/{sex}_HEIGHT_DATA.tsv'
    text = subprocess.check_output(['git', 'show', f'{HISTORICAL}:{path}'], cwd=ROOT, text=True)
    return list(csv.DictReader(io.StringIO(text), delimiter='\t'))


def generate(sex, originals, existing, sources):
    if len(originals) != len(existing):
        raise ValueError(f'{sex}: row count differs from the original export')
    out, counts = [], {}
    for i, (old, current) in enumerate(zip(originals, existing), 1):
        if old['height_cm'] != current['height_cm']:
            raise ValueError(f'{sex} row {i}: stature differs from the original')
        for field, choices in PAIRS.items():
            present = [old[k] for k in choices if old[k]]
            if len(present) > 1 or current[field] != (present[0] if present else ''):
                raise ValueError(f'{sex} row {i}: {field} differs from the original')
        result = dict(current)
        circ = old['head_circumference_cm']
        if circ:
            source = old['source']
            if source not in SOURCES:
                raise ValueError(f'{sex} row {i}: unknown circumference source {source}')
            _, _, circ_col, stature_col = SOURCES[source]
            record = sources[source][old['subject_id']]
            divisor = Decimal(10) if source == 'ANSUR_II' else Decimal(1)
            if (Decimal(record[circ_col]) / divisor != Decimal(circ) or
                    Decimal(record[stature_col]) / divisor != Decimal(old['height_cm'])):
                raise ValueError(f'{sex} row {i}: circumference/stature mismatch')
            source_sex = record['Gender'].upper() if source == 'ANSUR_II' else {'1': 'MALE', '2': 'FEMALE'}.get(record['HSSEX'])
            if source_sex != sex:
                raise ValueError(f'{sex} row {i}: sex mismatch')
            result['head_proxy_cm'] = f'{head_estimate(sex, circ):.6f}'
            counts[source] = counts.get(source, 0) + 1
        else:
            result['head_proxy_cm'] = ''
        out.append(result)
    return out, counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, default=DATA)
    args = parser.parse_args()
    sources = inputs()
    # Prepare and validate both files before writing either one.
    generated = {}
    for sex in FORMULAS:
        path = DATA / f'{sex}_HEIGHT_DATA.tsv'
        with path.open(newline='', encoding='utf-8') as f:
            reader = csv.DictReader(f, delimiter='\t')
            if reader.fieldnames != HEADER:
                raise ValueError(f'{path}: unexpected columns')
            generated[sex] = generate(sex, historical_rows(sex), list(reader), sources)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for sex, (rows, counts) in generated.items():
        with (args.output_dir / f'{sex}_HEIGHT_DATA.tsv').open('w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=HEADER, delimiter='\t', lineterminator='\n')
            writer.writeheader()
            writer.writerows(rows)
        print(sex, len(rows), counts)


if __name__ == '__main__':
    main()
