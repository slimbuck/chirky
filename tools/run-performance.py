#!/usr/bin/python3
"""Privileged test supervisor: capture GPU trace, run as host user, restore host.

Usage: sudo python3 tools/run-performance.py OUTPUT_NAME [benchmark arguments]
Output is confined to build/performance/OUTPUT_NAME. Normal host binary unchanged.
"""
import json
import os
from pathlib import Path
import pwd
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def systemctl(*args):
    return subprocess.run(['systemctl', *args], check=True, capture_output=True, text=True)


def telemetry():
    result = {}
    for key, args in [('temperature', ['measure_temp']), ('throttling', ['get_throttled']),
                      ('arm_clock', ['measure_clock', 'arm']), ('gpu_clock', ['measure_clock', 'v3d'])]:
        result[key] = subprocess.check_output(['vcgencmd', *args], text=True).strip()
    return result


def main():
    assert os.geteuid() == 0, 'Run with sudo to manage DRM ownership and tracefs.'
    name = sys.argv[1]
    assert name and all(c.isalnum() or c in '-_' for c in name)
    out = ROOT / 'build/performance' / name
    out.mkdir(parents=True, exist_ok=False)
    status_path = ROOT / 'run/status.json'
    status = json.loads(status_path.read_text()) if status_path.exists() else {}
    host_active = subprocess.run(['systemctl', 'is-active', '--quiet', 'chirky.service']).returncode == 0
    collector_active = subprocess.run(['systemctl', 'is-active', '--quiet', 'chirky-gpu.service']).returncode == 0
    user = systemctl('show', 'chirky.service', '-p', 'User', '--value').stdout.strip() or 'retro'
    account = pwd.getpwnam(user)
    os.chown(out, account.pw_uid, account.pw_gid)
    trace = Path('/sys/kernel/tracing/instances/chirky-benchmark')
    events = ['vc4_submit_cl_ioctl', 'vc4_submit_cl', 'vc4_bcl_end_irq', 'vc4_rcl_end_irq',
              'vc4_wait_for_seqno_begin', 'vc4_wait_for_seqno_end']
    process = None
    marker = None
    trace_created = False
    metadata = {'before': telemetry(), 'original_status': status,
                'uname': subprocess.check_output(['uname', '-a'], text=True).strip()}

    def interrupted(*_):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        systemctl('stop', 'chirky.service')
        systemctl('stop', 'chirky-gpu.service')
        trace.mkdir()  # Do not overwrite an existing tracing session.
        trace_created = True
        (trace / 'tracing_on').write_text('0')
        (trace / 'trace_clock').write_text('mono')
        (trace / 'buffer_size_kb').write_text('16384')
        for event in events:
            (trace / 'events/vc4' / event / 'enable').write_text('1')
        marker = os.open(trace / 'trace_marker', os.O_WRONLY)
        env = dict(os.environ, CHIRKY_BENCH_MARKER_FD=str(marker))
        (trace / 'tracing_on').write_text('1')
        command = [str(ROOT / 'build/performance-benchmark'), str(out / 'frames.csv'), *sys.argv[2:]]
        metadata['command'] = command
        with (out / 'run.log').open('w') as log:
            process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                       user=account.pw_uid, group=account.pw_gid,
                                       extra_groups=os.getgrouplist(user, account.pw_gid),
                                       env=env, pass_fds=(marker,))
            print(f'Benchmark PID {process.pid}; output {out}', flush=True)
            began = time.monotonic()
            metadata['thermal_samples'] = []
            while process.poll() is None:
                if time.monotonic() - began > 420:
                    raise TimeoutError('Benchmark exceeded seven-minute safety timeout')
                metadata['thermal_samples'].append({'elapsed': time.monotonic()-began, **telemetry()})
                time.sleep(2)
            metadata['exit_code'] = process.returncode
            metadata['elapsed'] = time.monotonic() - began
            if process.returncode:
                raise RuntimeError(f'Benchmark failed: {process.returncode}; see {out}/run.log')
    finally:
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=12)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        try:
            if trace_created:
                (trace / 'tracing_on').write_text('0')
                try:
                    (out / 'gpu-trace.txt').write_text((trace / 'trace').read_text())
                    metadata['trace_stats'] = {p.parent.name: p.read_text() for p in (trace / 'per_cpu').glob('cpu*/stats')}
                finally:
                    if marker is not None:
                        os.close(marker)
                    for event in events:
                        enable = trace / 'events/vc4' / event / 'enable'
                        if enable.exists():
                            enable.write_text('0')
                    trace.rmdir()
            metadata['after'] = telemetry()
            (out / 'metadata.json').write_text(json.dumps(metadata, indent=2))
        finally:
            # Even a trace read or telemetry failure must restore the console.
            try:
                if host_active:
                    systemctl('start', 'chirky.service')
                    for _ in range(50):
                        if status_path.exists() and (ROOT / 'run/control.fifo').exists():
                            break
                        time.sleep(0.1)
                    commands = []
                    if status.get('game'):
                        commands.append('launch ' + status['game'])
                    if status.get('profiling'):
                        commands.append('timing on')
                    for command in commands:
                        fd = os.open(ROOT / 'run/control.fifo', os.O_WRONLY | os.O_NONBLOCK)
                        try:
                            os.write(fd, (command + '\n').encode())
                        finally:
                            os.close(fd)
                        time.sleep(0.2)
            finally:
                if collector_active:
                    systemctl('start', 'chirky-gpu.service')
            print('Normal services restored.', flush=True)


if __name__ == '__main__':
    main()
