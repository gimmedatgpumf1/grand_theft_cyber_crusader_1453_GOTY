#!/usr/bin/env python3
"""Export the three directly measured, same-subject Snyder dimensions in cm."""
import argparse
import csv
from decimal import Decimal
import hashlib
from pathlib import Path

SOURCE_SHA256 = '4387d090698f0d015b817dc5992c4881df1473e3bf5ab7a7562ab1c812535ebf'
FIELDS = ('Stature', 'Head_Height', 'Head_Circumference')
DEFAULT_OUTPUT = Path(__file__).resolve().parents[1] / 'anthropometry/HEAD_MEASUREMENTS_PAIRED.tsv'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='pinned PIPER snyder.csv')
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    if hashlib.sha256(args.source.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise SystemExit('Source SHA-256 does not match the pinned PIPER file.')
    if args.source.resolve() == args.output.resolve():
        raise SystemExit('Output must not overwrite the source.')
    with args.source.open(newline='', encoding='utf-8-sig') as source, \
         args.output.open('w', newline='', encoding='utf-8') as output:
        reader = csv.DictReader(source)
        if not set(FIELDS).issubset(reader.fieldnames or []):
            raise SystemExit('Source does not contain the expected columns.')
        writer = csv.writer(output, delimiter='\t', lineterminator='\n')
        writer.writerow(['height_cm', 'head_height_cm', 'head_circumference_cm'])
        count = 0
        for row in reader:
            if not all(row[k] and Decimal(row[k]) > 0 for k in FIELDS):
                continue
            writer.writerow([f'{Decimal(row[k]) / 10:.1f}' for k in FIELDS])
            count += 1
    print(f'Exported {count} same-subject measurements to {args.output}')


if __name__ == '__main__':
    main()
