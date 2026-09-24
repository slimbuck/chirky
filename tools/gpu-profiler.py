#!/usr/bin/python3
"""VC4 dispatch-to-completion timings. Runs as a separate privileged service.

Only writes its own /run file and uses its own tracefs instance. The host reads
the shared ring without blocking. Values include kernel/interrupt latency.
"""
import os
import json
import sys
from pathlib import Path
import re
import signal
import struct
import time

MAGIC = 0x434849524B594750
SLOTS = 256
SIZE = 32 + SLOTS * 32
EVENT = re.compile(r"-(\d+)\s+\[\d+\].*? (\d+)\.(\d+): (vc4_\w+):.*?dev=(\d+).*?seqno=(\d+)")


class Jobs:
    def __init__(self):
        self.pending = {}

    def feed(self, line):
        match = EVENT.search(line)
        if not match:
            return None
        pid, sec, fraction, event, dev, seq = match.groups()
        stamp = int(sec) * 1000000 + int(fraction.ljust(6, '0')[:6])
        key = (int(dev), int(seq))
        if event == 'vc4_submit_cl' and 'BCL,' in line:
            self.pending[key] = (stamp, int(pid))
        elif event == 'vc4_rcl_end_irq':
            start = self.pending.pop(key, None)
            if start and stamp >= start[0]:
                return start[0], stamp, start[1]
        return None


def run():
    root = Path('/run/chirky-gpu')
    root.mkdir(mode=0o755, exist_ok=True)
    trace = Path('/sys/kernel/tracing/instances/chirky-gpu')
    trace.mkdir(exist_ok=True)
    events = ['vc4_submit_cl', 'vc4_rcl_end_irq']
    memory = bytearray(SIZE)
    struct.pack_into('<Q', memory, 0, MAGIC)
    published = 0
    jobs = Jobs()
    total = 0
    watermark = 0
    pipe = None
    running = True

    def stop(*_):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        (trace / 'tracing_on').write_text('0')
        (trace / 'trace_clock').write_text('mono')
        (trace / 'buffer_size_kb').write_text('256')
        (trace / 'trace').write_text('')
        for event in events:
            (trace / 'events/vc4' / event / 'enable').write_text('1')
        pipe = os.open(trace / 'trace_pipe', os.O_RDONLY | os.O_NONBLOCK)
        buffer = ''
        enabled = False
        while running:
            # The collector is dormant when the overlay is hidden. Status is
            # an atomic, bounded JSON file; no commands or paths come from it.
            try:
                fd = os.open(sys.argv[1], os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
                with os.fdopen(fd) as status:
                    requested = json.loads(status.read(4096)).get('profiling') is True
            except (OSError, ValueError, IndexError):
                requested = False
            if requested != enabled:
                (trace / 'tracing_on').write_text('0')
                (trace / 'trace').write_text('')
                jobs.pending.clear()
                buffer = ''
                total = 0
                (trace / 'tracing_on').write_text('1' if requested else '0')
                if not requested:
                    (root / 'samples').unlink(missing_ok=True)
                enabled = requested
            if not enabled:
                time.sleep(0.25)
                continue
            # Ten batched reads per second keep profiling overhead low. The
            # event timestamps still retain microsecond resolution.
            time.sleep(0.1)
            before = time.monotonic_ns() // 1000
            samples = []
            # Drain before publishing a watermark: a completed host frame is
            # evaluated only once all earlier trace events have been consumed.
            while True:
                try:
                    chunk = os.read(pipe, 65536)
                except BlockingIOError:
                    break
                if not chunk:
                    break
                buffer += chunk.decode('ascii', errors='replace')
                while '\n' in buffer:
                    line, buffer = buffer.split('\n', 1)
                    if 'LOST' in line:
                        samples.append((watermark, before, 0))
                        jobs.pending.clear()
                    sample = jobs.feed(line)
                    if sample:
                        samples.append(sample)
            for key, (start, _) in list(jobs.pending.items()):
                if before - start > 1000000:
                    samples.append((start, before, 0))
                    del jobs.pending[key]
            # An incomplete text record or active job must delay the watermark.
            end = min([before] + [start for start, _ in jobs.pending.values()])
            if buffer:
                end = watermark
            for start, finish, pid in samples:
                total += 1
                struct.pack_into('<QQQQ', memory, 32 + ((total - 1) % SLOTS) * 32,
                                 total, start, finish, pid)
            watermark = max(watermark, end)
            struct.pack_into('<QQ', memory, 16, watermark, total)
            if before - published >= 20000:
                # Immutable snapshots avoid torn reads across processes, even
                # if the collector crashes or restarts during publication.
                fd = os.open(root / 'samples.next', os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o644)
                try:
                    with os.fdopen(fd, 'wb') as output:
                        output.write(memory)
                    os.replace(root / 'samples.next', root / 'samples')
                finally:
                    (root / 'samples.next').unlink(missing_ok=True)
                published = before
    finally:
        (trace / 'tracing_on').write_text('0')
        for event in events:
            path = trace / 'events/vc4' / event / 'enable'
            if path.exists():
                path.write_text('0')
        if pipe is not None:
            os.close(pipe)
        (root / 'samples').unlink(missing_ok=True)
        trace.rmdir()


if __name__ == '__main__':
    run()
