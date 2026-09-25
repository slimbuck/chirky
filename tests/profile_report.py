import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('profile_report', ROOT / 'tools/profile-report.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.data = json.loads((ROOT / 'build/test-profile.json').read_text())

    def test_cpu_waits_and_nested_costs(self):
        result = report.analyse(self.data)
        rows = {row['name']: row for row in result['scopes']}
        self.assertEqual(rows['robot']['mean_cpu_us'], 1000)
        self.assertEqual(rows['robot']['mean_self_cpu_us'], 500)
        self.assertEqual(rows['robot']['mean_rectangles'], 100)
        self.assertEqual(rows['present.wait']['p95_elapsed_us'], 15664)
        self.assertEqual(rows['present.wait']['mean_cpu_us'], 10)
        self.assertEqual(sum(row['mean_self_cpu_us'] for row in rows.values()), 1013)

    def test_absent_scopes_count_as_zero(self):
        self.data['traceEvents'][5]['name'] = 'other'
        rows = {row['name']: row for row in report.analyse(self.data)['scopes']}
        self.assertEqual(rows['robot']['mean_cpu_us'], 500)

    def test_missing_gpu_is_not_zero(self):
        self.data['traceEvents'][0]['args']['gpu_us'] = 2500
        result = report.analyse(self.data)
        self.assertEqual(result['gpu_valid_frames'], 1)
        self.assertEqual(result['gpu_mean_us'], 2500)

    def test_sprite_counts_and_older_captures(self):
        self.data.update(sprites=2, batches=6)
        result = report.analyse(self.data)
        self.assertEqual(result['mean_sprites'], 1)
        self.assertEqual(result['mean_batches'], 3)
        del self.data['sprites']
        self.assertIsNone(report.analyse(self.data)['mean_sprites'])

    def test_cpu_spikes_are_ranked_separately_from_waits(self):
        self.data['traceEvents'][4]['args']['cpu_us'] = 2000
        self.data['traceEvents'][0]['dur'] += 100
        result = report.analyse(self.data)
        self.assertEqual(result['worst_frames'][0]['frame'], 0)
        self.assertEqual(result['cpu_heaviest_frames'][0]['frame'], 1)

    def test_corrupt_captures_rejected(self):
        for mutate in (
            lambda d: d.update(valid=False),
            lambda d: d.update(frames=3),
            lambda d: d.update(missed=1),
            lambda d: d['traceEvents'][2]['args'].update(parent=999),
            lambda d: d['traceEvents'][2].update(dur=99999),
        ):
            data = copy.deepcopy(self.data)
            mutate(data)
            with self.assertRaises(ValueError):
                report.analyse(data)


if __name__ == '__main__':
    unittest.main()
