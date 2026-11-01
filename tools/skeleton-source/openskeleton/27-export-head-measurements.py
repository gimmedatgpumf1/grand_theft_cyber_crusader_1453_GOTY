#!/usr/bin/env python3
"""Export same-subject Snyder head dimensions in separate sex-coded files."""
import argparse
import csv
from decimal import Decimal
import hashlib
from pathlib import Path

SOURCE_SHA256 = '4387d090698f0d015b817dc5992c4881df1473e3bf5ab7a7562ab1c812535ebf'
FIELDS = ('Stature', 'Head_Height', 'Head_Circumference')
HEADER = ['height_cm', 'head_height_cm', 'head_circumference_cm']
DEFAULT_OUTPUT = Path(__file__).resolve().parents[1] / 'anthropometry'
FILENAMES = {'1': 'MALE_HEAD_MEASUREMENTS_PAIRED.tsv',
             '2': 'FEMALE_HEAD_MEASUREMENTS_PAIRED.tsv'}


def export(source, output_dir):
    if hashlib.sha256(source.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError('Source SHA-256 does not match the pinned PIPER file.')
    rows = {'1': [], '2': []}
    with source.open(newline='', encoding='utf-8-sig') as f:
        reader = csv.DictReader(f)
        if not set(FIELDS + ('Gender',)).issubset(reader.fieldnames or []):
            raise ValueError('Source does not contain the expected columns.')
        for row in reader:
            if not all(row[k] and Decimal(row[k]) > 0 for k in FIELDS):
                continue
            sex = row['Gender']
            if sex not in rows:
                raise ValueError(f'Unknown sex code {sex!r} on a complete measurement row')
            rows[sex].append([f'{Decimal(row[k]) / 10:.1f}' for k in FIELDS])
    output_dir.mkdir(parents=True, exist_ok=True)
    for sex, name in FILENAMES.items():
        path = output_dir / name
        if source.resolve() == path.resolve():
            raise ValueError('Output must not overwrite the source.')
        with path.open('w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f, delimiter='\t', lineterminator='\n')
            writer.writerow(HEADER)
            writer.writerows(rows[sex])
    return {sex: len(values) for sex, values in rows.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='pinned PIPER snyder.csv')
    parser.add_argument('--output-dir', type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    print('Exported source sex codes:', export(args.source, args.output_dir))


if __name__ == '__main__':
    main()
