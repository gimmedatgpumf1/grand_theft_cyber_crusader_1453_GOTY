#!/usr/bin/env python3
"""Validate every updated head value and preservation of the other columns."""
import csv
from decimal import Decimal
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
script = root / 'tools/skeleton-source/openskeleton/28-update-whole-body-head.py'
spec = importlib.util.spec_from_file_location('whole_body_head', script)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
assert mod.head_estimate('MALE', '50') == Decimal('18.1424')
assert mod.head_estimate('FEMALE', '50') == Decimal('18.1676')
source_records = mod.inputs()
expected_counts = {
    'MALE': {'NHANES_III_INFANT': 1536, 'NHANES_III_CHILD': 2638, 'ANSUR_II': 4082},
    'FEMALE': {'NHANES_III_INFANT': 1518, 'NHANES_III_CHILD': 2727, 'ANSUR_II': 1986},
}
for sex in ('MALE', 'FEMALE'):
    path = mod.DATA / f'{sex}_HEIGHT_DATA.tsv'
    with path.open(newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f, delimiter='\t')
        assert reader.fieldnames == mod.HEADER
        current = list(reader)
    old = mod.historical_rows(sex)
    generated, counts = mod.generate(sex, old, current, source_records)
    assert counts == expected_counts[sex]
    assert current == generated, f'{sex}: current TSV does not match the requested formulas'
    assert len(current) == (18506 if sex == 'MALE' else 17644)
    # Regression: missing circumferences stay blank; infant observations are used.
    assert all(not row['head_proxy_cm'] for row, original in zip(current, old)
               if not original['head_circumference_cm'])
    assert sum(bool(row['head_proxy_cm']) for row, original in zip(current, old)
               if original['source'] == 'NHANES_III_INFANT') == counts['NHANES_III_INFANT']

# Running a second time must be deterministic and must not alter the source files.
with tempfile.TemporaryDirectory() as tmp:
    subprocess.run([sys.executable, str(script), '--output-dir', tmp], check=True, cwd=root)
    for sex in ('MALE', 'FEMALE'):
        name = f'{sex}_HEIGHT_DATA.tsv'
        assert (mod.DATA / name).read_bytes() == (Path(tmp) / name).read_bytes()
print('Whole-body head estimates, same-subject sources, and preserved columns verified.')
