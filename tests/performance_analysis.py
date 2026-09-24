"""Check attribution when GPU completion crosses CPU frame boundaries."""
import contextlib
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('analysis', Path(__file__).resolve().parents[1] / 'tools/analyse-performance.py')
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class AnalysisTests(unittest.TestCase):
    def fixture(self, directory):
        rows = []
        for frame, start, submit, present in [(1, 1000, 1300, 4000), (2, 1500, 4400, 6000)]:
            row = dict(pid=42, id=frame, stage=0, name='test', mode=1, index=10+frame,
                       loops=0, shader=0, layers=1, scene=0)
            for i, phase in enumerate(['start','work','draw','finish','prewait','swap','lock','fb','submit']):
                row[phase+'_wall'] = start+(submit-start)*i//8
                row[phase+'_cpu'] = 1000*frame+i*10
            row.update(present=present, callback=present, sequence=frame, missed=0)
            rows.append(row)
        with (directory/'frames.csv').open('w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=rows[0]); writer.writeheader(); writer.writerows(rows)
        events = [(1000,'tracing_mark_write','chirky-bench frame=1 start'),
                  (1100,'vc4_submit_cl_ioctl',''), (1200,'vc4_submit_cl','BCL, seqno=1'),
                  (1210,'vc4_bcl_end_irq','seqno=1'), (1210,'vc4_submit_cl','RCL, seqno=1'),
                  (1500,'tracing_mark_write','chirky-bench frame=2 start'),
                  (3000,'vc4_rcl_end_irq','seqno=1'),
                  (4200,'vc4_submit_cl_ioctl',''), (4300,'vc4_submit_cl','BCL, seqno=2'),
                  (4310,'vc4_bcl_end_irq','seqno=2'), (4310,'vc4_submit_cl','RCL, seqno=2'),
                  (5000,'vc4_rcl_end_irq','seqno=2')]
        (directory/'gpu-trace.txt').write_text('\n'.join(
            f'bench-42 [000] .... 0.{stamp:06}: {event}: {detail}' for stamp,event,detail in events))
        (directory/'metadata.json').write_text(json.dumps({'trace_stats':{'cpu0':'overrun: 0\n'}}))

    def test_pipeline_attribution_and_cpu_wait_exclusion(self):
        with tempfile.TemporaryDirectory() as path:
            p = Path(path); self.fixture(p)
            with contextlib.redirect_stdout(io.StringIO()):
                analysis.analyse(p)
            with (p/'joined.csv').open() as f:
                a,b = list(csv.DictReader(f))
            self.assertEqual((a['gpu_us'],b['gpu_us']), ('1800','700'))
            self.assertEqual(b['cpu_overlaps_previous_gpu'], 'True')
            self.assertEqual(b['cpu_us'], '80')
            self.assertEqual(b['start_to_submit_us'], '2900')

    def test_truncated_and_overrun_captures_rejected(self):
        for failure in ['incomplete', 'overrun']:
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as path:
                p = Path(path); self.fixture(p)
                if failure == 'incomplete':
                    trace = p/'gpu-trace.txt'
                    trace.write_text('\n'.join(trace.read_text().splitlines()[:-1]))
                else:
                    (p/'metadata.json').write_text(json.dumps({'trace_stats':{'cpu0':'overrun: 1\n'}}))
                with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(ValueError):
                    analysis.analyse(p)


if __name__ == '__main__':
    unittest.main()
