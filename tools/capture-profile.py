#!/usr/bin/env python3
"""Record live play on the Pi, without restarting the host or changing input."""
import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]


def capture(frames, timeout, output):
    fifo = ROOT / 'run/control.fifo'
    result = ROOT / 'run/profile.json'
    if output.exists():
        raise ValueError(f'Output already exists: {output}')
    if output.resolve() in (result.resolve(), (ROOT / 'run/profile.json.tmp').resolve()):
        raise ValueError('Choose an output outside the host staging files')
    output.parent.mkdir(parents=True, exist_ok=True)
    # Check actual directory access before asking the host to record.
    with tempfile.TemporaryFile(dir=output.parent):
        pass
    request_id = uuid.uuid4().hex
    fd = os.open(fifo, os.O_WRONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    try:
        if not stat.S_ISFIFO(os.fstat(fd).st_mode):
            raise ValueError('Control channel is not a FIFO')
        os.write(fd, f'capture {frames} {request_id}\n'.encode())
    finally:
        os.close(fd)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            data = json.loads(result.read_text())
            if data.get('request_id') == request_id:
                if data['target'] != frames:
                    raise ValueError('Another capture completed; retry when it is finished')
                with output.open('x') as stream:
                    json.dump(data, stream)
                return data
        except FileNotFoundError:
            pass
        time.sleep(.1)
    raise TimeoutError('No capture received. Host must include live profiling; check its log for rejected/busy captures.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--timeout', type=float, default=180)
    args = parser.parse_args()
    if not 1 <= args.frames <= 1800 or args.timeout <= 0:
        parser.error('frames must be 1..1800 and timeout must be positive')
    try:
        capture(args.frames, args.timeout, args.output)
    except (OSError, ValueError, TimeoutError) as error:
        parser.exit(1, f'{error}\n')
    print(f'Saved {args.output}', flush=True)
    raise SystemExit(subprocess.call([os.sys.executable, str(ROOT / 'tools/profile-report.py'), str(args.output)]))


if __name__ == '__main__':
    main()
