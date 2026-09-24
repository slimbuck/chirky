import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('profiler', Path(__file__).parents[1] / 'tools/gpu-profiler.py')
profiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profiler)


class JobsTest(unittest.TestCase):
    def test_dispatch_completion(self):
        jobs = profiler.Jobs()
        self.assertIsNone(jobs.feed(' chirky-host-123 [002] d..1.  12.000100: vc4_submit_cl: dev=0, BCL, seqno=7, 0x0..0x1'))
        self.assertIsNone(jobs.feed(' <idle>-0 [003] d.h2.  12.000200: vc4_submit_cl: dev=0, RCL, seqno=7, 0x0..0x1'))
        self.assertEqual(jobs.feed(' <idle>-0 [003] d.h2.  12.000700: vc4_rcl_end_irq: dev=0, seqno=7'), (12000100, 12000700, 123))
        self.assertIsNone(jobs.feed(' <idle>-0 [003] d.h2.  12.000800: vc4_rcl_end_irq: dev=0, seqno=7'))

    def test_device_isolation_and_unmatched(self):
        jobs = profiler.Jobs()
        jobs.feed(' host-42 [000] ...  1.1: vc4_submit_cl: dev=0, BCL, seqno=1, 0x0..0x1')
        self.assertIsNone(jobs.feed(' idle-0 [001] ...  1.2: vc4_rcl_end_irq: dev=1, seqno=1'))
        self.assertEqual(jobs.feed(' idle-0 [001] ...  1.3: vc4_rcl_end_irq: dev=0, seqno=1'), (1100000, 1300000, 42))
        self.assertIsNone(jobs.feed('# comment'))


if __name__ == '__main__':
    unittest.main()
