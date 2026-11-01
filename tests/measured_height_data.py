#!/usr/bin/env python3
"""Regression checks for the measured-only export; run with Python 3."""
import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'tools/skeleton-source/anthropometry'
SCRIPT = ROOT / 'tools/skeleton-source/openskeleton/27-export-measured-height-data.py'
spec = importlib.util.spec_from_file_location('measured_height_export', SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MeasuredHeightExportTest(unittest.TestCase):
    def test_checked_in_export_reproduces_from_source_excerpt(self):
        with tempfile.TemporaryDirectory() as tmp:
            with (DATA / 'SLOVAK_EXCERPT.csv').open(newline='', encoding='utf-8') as f:
                counts = module.export(csv.DictReader(f), Path(tmp))
            self.assertEqual(counts, {'M': 29, 'F': 44})
            for sex in ('MALE', 'FEMALE'):
                filename = sex + '_HEIGHT_DATA.tsv'
                self.assertEqual((Path(tmp) / filename).read_bytes(), (DATA / filename).read_bytes())
                with (Path(tmp) / filename).open(newline='') as f:
                    rows = list(csv.reader(f, delimiter='\t'))
                self.assertEqual(rows[0], module.HEADER)
                for row in rows[1:]:
                    self.assertEqual(len(row), 8)
                    self.assertEqual(row[1:7], [''] * 6)
                    self.assertGreater(float(row[0]), 0)
                    self.assertTrue(15 <= float(row[7]) <= 70)

    def test_missing_invalid_and_outlier_values_are_not_imputed(self):
        rows = [
            {'sex': 'F', 'age': 20, 'height_cm': 160, 'shoulder_breadth_cm': 40},
            {'sex': 'M', 'age': 19, 'height_cm': 180, 'shoulder_breadth_cm': 102},
            {'sex': 'M', 'age': 19, 'height_cm': 180, 'shoulder_breadth_cm': None},
            {'sex': 'M', 'age': 17, 'height_cm': 180, 'shoulder_breadth_cm': 40},
            {'sex': 'F', 'age': 20, 'height_cm': 'nan', 'shoulder_breadth_cm': 40},
        ]
        with tempfile.TemporaryDirectory() as tmp:
            counts = module.export(rows, Path(tmp))
            self.assertEqual(counts, {'M': 0, 'F': 1})
            self.assertEqual(len((Path(tmp) / 'MALE_HEIGHT_DATA.tsv').read_text().splitlines()), 1)

    def test_workbook_header_mapping(self):
        try:
            from openpyxl import Workbook
        except ImportError:
            self.skipTest('openpyxl is needed only for xlsx imports')
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'table.xlsx'
            book = Workbook()
            sheet = book.active
            sheet.append(['title'])
            sheet.append(['Gender', 'Age', '2.Stature height (cm)', '8.Shoulder (bideltoid) breadth (cm)'])
            sheet.append(['F', 23, 164, 35])
            book.save(path)
            rows = list(module.workbook_rows(path))
            self.assertEqual(rows, [{'sex': 'F', 'age': 23, 'height_cm': 164, 'shoulder_breadth_cm': 35}])


if __name__ == '__main__':
    unittest.main()
