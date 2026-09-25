import importlib.util
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('capture_profile', ROOT / 'tools/capture-profile.py')
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class CaptureTests(unittest.TestCase):
    def test_unwritable_destination_fails_before_control_command(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(capture.tempfile, 'TemporaryFile', side_effect=PermissionError):
                with patch.object(capture.os, 'open') as control:
                    with self.assertRaises(PermissionError):
                        capture.capture(2, 3, Path(directory) / 'capture.json')
                    control.assert_not_called()

    def test_fifo_capture_ignores_stale_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'run').mkdir()
            fifo = root / 'run/control.fifo'
            result = root / 'run/profile.json'
            os.mkfifo(fifo)
            result.write_text(json.dumps({'request_id': 'stale', 'target': 2}))
            fd = os.open(fifo, os.O_RDWR)
            errors = []

            def host():
                try:
                    command = os.read(fd, 256).decode().split()
                    self.assertEqual(command[:2], ['capture', '2'])
                    data = json.loads((ROOT / 'build/test-profile.json').read_text())
                    data['request_id'] = command[2]
                    temporary = result.with_suffix('.tmp')
                    temporary.write_text(json.dumps(data))
                    temporary.replace(result)
                except Exception as error:
                    errors.append(error)

            worker = threading.Thread(target=host)
            worker.start()
            previous = capture.ROOT
            capture.ROOT = root
            try:
                output = root / 'captures/new.json'
                data = capture.capture(2, 3, output)
                self.assertNotEqual(data['request_id'], 'stale')
                self.assertEqual(json.loads(output.read_text()), data)
                with self.assertRaises(ValueError):
                    capture.capture(2, 3, output)
            finally:
                capture.ROOT = previous
                worker.join(timeout=3)
                os.close(fd)
            self.assertFalse(worker.is_alive())
            self.assertFalse(errors, errors)


if __name__ == '__main__':
    unittest.main()
