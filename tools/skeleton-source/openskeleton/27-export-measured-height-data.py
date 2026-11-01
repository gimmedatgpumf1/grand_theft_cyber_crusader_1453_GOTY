#!/usr/bin/env python3
"""Export direct Slovak stature and source shoulder breadth, no predictions.

Default: export the small, checked-in transcription. Pass --xlsx Table\ 2.xlsx
for the full source workbook. See MEASURED_HEIGHT_DATA.md for caveats.
"""
import argparse
import csv
import hashlib
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / 'anthropometry'
HEADER = ['height_cm', 'head_cm', 'neck_cm', 'torso_cm', 'arm_cm',
          'leg_cm', 'pelvis_cm', 'shoulder_breadth_source_cm']


def workbook_rows(path):
    try:
        from openpyxl import load_workbook
    except ImportError as exc:
        raise SystemExit('Install openpyxl to read the original xlsx workbook.') from exc
    book = load_workbook(path, read_only=True, data_only=True)
    try:
        sheet = book.worksheets[0]
        rows = iter(sheet.iter_rows(values_only=True))
        for line in rows:
            vals = [str(v).strip() if v is not None else '' for v in line]
            if '2.Stature height (cm)' in vals and '8.Shoulder (bideltoid) breadth (cm)' in vals:
                idx = {name: vals.index(name) for name in ('Gender', 'Age', '2.Stature height (cm)', '8.Shoulder (bideltoid) breadth (cm)')}
                break
        else:
            raise ValueError('Expected source headers not found in first worksheet')
        for line in rows:
            def at(name):
                i = idx[name]
                return line[i] if i < len(line) else None
            yield {'sex': at('Gender'), 'age': at('Age'),
                   'height_cm': at('2.Stature height (cm)'),
                   'shoulder_breadth_cm': at('8.Shoulder (bideltoid) breadth (cm)')}
    finally:
        book.close()


def numeric(value):
    if value is None or str(value).strip() == '':
        return None
    try:
        result = float(value)
        return result if math.isfinite(result) else None
    except (ValueError, TypeError):
        return None


def export(rows, output):
    grouped = {'M': [], 'F': []}
    for row in rows:
        sex = str(row['sex']).strip().upper()
        age = numeric(row['age'])
        height = numeric(row['height_cm'])
        shoulder = numeric(row['shoulder_breadth_cm'])
        if sex not in grouped or age is None or not 18 <= age <= 25 or height is None or height <= 0:
            continue
        # Source outlier 102 cm occurs alongside a visibly corrupted row; do not
        # turn outliers into estimates. This is a sanity flag, not a correction.
        if shoulder is not None and not 15 <= shoulder <= 70:
            shoulder = None
        if shoulder is None:
            continue  # No usable target remains for this person.
        grouped[sex].append([height, '', '', '', '', '', '', shoulder])
    output.mkdir(parents=True, exist_ok=True)
    for sex, name in [('M', 'MALE_HEIGHT_DATA.tsv'), ('F', 'FEMALE_HEIGHT_DATA.tsv')]:
        with (output / name).open('w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f, delimiter='\t', lineterminator='\n')
            writer.writerow(HEADER)
            writer.writerows(grouped[sex])
    return {sex: len(data) for sex, data in grouped.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xlsx', type=Path, help='original Table 2.xlsx (instead of checked-in excerpt)')
    parser.add_argument('--output', type=Path, default=ROOT)
    args = parser.parse_args()
    if args.xlsx:
        digest = hashlib.sha256(args.xlsx.read_bytes()).hexdigest()
        if digest != 'a92af0f64164c754bb3888cdb94cd6484134808dc532385371126066b338cbb2':
            raise SystemExit('Workbook SHA-256 does not match Mendeley Table 2, version 3.')
        counts = export(workbook_rows(args.xlsx), args.output)
    else:
        with (ROOT / 'SLOVAK_EXCERPT.csv').open(newline='', encoding='utf-8') as f:
            counts = export(csv.DictReader(f), args.output)
    print('Exported directly measured shoulder breadth:', counts)


if __name__ == '__main__':
    main()
