#!/usr/bin/python3
"""Join frame phases and VC4 jobs; write reproducible per-frame and stage CSVs."""
from collections import defaultdict, deque
import csv
import json
from pathlib import Path
import re
import statistics as stat
import sys

LINE = re.compile(r'-(\d+)\s+\[\d+\].*? (\d+)\.(\d+): (\w+): (.*)')


def percentile(values, fraction):
    values = sorted(values)
    return values[min(len(values)-1, int((len(values)-1)*fraction))] if values else None


def union(intervals):
    result = []
    for begin, end in sorted(intervals):
        if result and begin <= result[-1][1]:
            result[-1][1] = max(result[-1][1], end)
        else:
            result.append([begin, end])
    return sum(end-begin for begin, end in result)


def analyse(directory):
    directory = Path(directory)
    with (directory / 'frames.csv').open() as source:
        frames = [{k: (v if k=='name' else int(v)) for k,v in r.items()} for r in csv.DictReader(source)]
    by_id = {r['id']: r for r in frames}
    pid = frames[0]['pid']
    current = 0
    pending = deque()
    jobs = {}
    by_frame = defaultdict(list)
    waiting = {}
    waits = []
    for line in (directory / 'gpu-trace.txt').read_text().splitlines():
        match = LINE.search(line)
        if not match:
            continue
        task, sec, fraction, event, detail = match.groups()
        stamp = int(sec)*1000000+int(fraction.ljust(6,'0')[:6])
        task = int(task)
        if event=='tracing_mark_write' and task==pid:
            frame = re.search(r'chirky-bench frame=(\d+)',detail)
            if frame:
                current = int(frame[1])
        elif event=='vc4_submit_cl_ioctl' and task==pid:
            pending.append({'frame': current, 'ioctl': stamp})
        elif event=='vc4_submit_cl':
            seq = int(re.search(r'seqno=(\d+)',detail)[1])
            if seq not in jobs:
                if not pending:
                    raise ValueError(f'GPU dispatch {seq} has no matching submission')
                jobs[seq] = pending.popleft()
                jobs[seq].update(seq=seq, begin=stamp)
            jobs[seq]['render' if 'RCL,' in detail else 'bin'] = stamp
        elif event in ('vc4_bcl_end_irq','vc4_rcl_end_irq'):
            seq = int(re.search(r'seqno=(\d+)',detail)[1])
            if seq not in jobs:
                raise ValueError(f'GPU completion {seq} has no dispatch')
            job = jobs[seq]
            job['bin_end' if event=='vc4_bcl_end_irq' else 'end'] = stamp
            if event=='vc4_rcl_end_irq':
                by_frame[job['frame']].append(job)
        elif event=='vc4_wait_for_seqno_begin' and task==pid:
            waiting[task] = (stamp,current)
        elif event=='vc4_wait_for_seqno_end' and task in waiting:
            begin,frame = waiting.pop(task)
            waits.append((frame, begin, stamp))
    if pending:
        raise ValueError(f'{len(pending)} GPU submissions never dispatched')
    if any('end' not in j for j in jobs.values()):
        raise ValueError('Incomplete GPU jobs in trace')
    if waiting:
        raise ValueError('Incomplete driver wait in trace')
    # No unrelated GL client should be present while the supervisor owns DRM.
    period = stat.median([(b['present']-a['present'])/(b['sequence']-a['sequence'])
                          for a,b in zip(frames,frames[1:]) if b['sequence']>a['sequence']])
    previous = None
    joined = []
    for f in frames:
        row = dict(f)
        gpu = by_frame[f['id']]
        row['jobs'] = len(gpu)
        row['gpu_us'] = union([(j['begin'],j['end']) for j in gpu]) if gpu else None
        row['gpu_busy_us'] = union([(j['bin'],j['bin_end']) for j in gpu if 'bin' in j and 'bin_end' in j]+
                                   [(j['render'],j['end']) for j in gpu if 'render' in j]) if gpu else None
        row['gpu_first'] = min([j['begin'] for j in gpu],default=None)
        row['gpu_last'] = max([j['end'] for j in gpu],default=None)
        row['gpu_queue_us'] = sum(j['begin']-j['ioctl'] for j in gpu)
        row['gpu_after_swap'] = bool(gpu and row['gpu_last']>f['swap_wall'])
        row['gpu_after_submit'] = bool(gpu and row['gpu_last']>f['submit_wall'])
        row['cpu_us'] = f['submit_cpu']-f['start_cpu']
        row['work_cpu_us'] = f['work_cpu']-f['start_cpu']
        row['work_wall_us'] = f['work_wall']-f['start_wall']
        row['draw_cpu_us'] = f['draw_cpu']-f['work_cpu']
        row['draw_wall_us'] = f['draw_wall']-f['work_wall']
        row['finish_wait_us'] = f['finish_wall']-f['draw_wall']
        row['previous_wait_us'] = f['prewait_wall']-f['finish_wall']
        row['swap_us'] = f['swap_wall']-f['prewait_wall']
        row['lock_us'] = f['lock_wall']-f['swap_wall']
        row['flip_ioctl_us'] = f['submit_wall']-f['fb_wall']
        row['start_to_submit_us'] = f['submit_wall']-f['start_wall']
        row['submit_to_callback_us'] = f['callback']-f['submit_wall']
        row['latency_us'] = f['callback']-f['start_wall']
        row['drm_timestamp_offset_us'] = f['present']-f['callback']
        row['driver_wait_us'] = sum(end-begin for frame,begin,end in waits if frame==f['id'])
        row['gpu_before_callback_us'] = f['callback']-row['gpu_last'] if gpu else None
        row['interval_us'] = f['present']-previous['present'] if previous else None
        row['callback_interval_us'] = f['callback']-previous['callback'] if previous else None
        row['gpu_deadline_margin_us'] = previous['callback']+period-row['gpu_last'] if previous and gpu else None
        row['submit_deadline_margin_us'] = previous['callback']+period-f['submit_wall'] if previous else None
        row['cpu_overlaps_previous_gpu'] = bool(previous and by_frame[previous['id']] and
            f['start_wall'] < max(j['end'] for j in by_frame[previous['id']]))
        joined.append(row)
        previous=f
    with (directory/'joined.csv').open('w',newline='') as out:
        writer=csv.DictWriter(out,fieldnames=list(joined[0]));writer.writeheader();writer.writerows(joined)
    groups=defaultdict(list)
    for r in joined:
        if r['index']>=12:groups[r['stage']].append(r)
    summaries=[]
    metrics=['cpu_us','work_cpu_us','work_wall_us','draw_cpu_us','draw_wall_us','gpu_us','gpu_busy_us',
             'gpu_queue_us','finish_wait_us','previous_wait_us','swap_us','lock_us','flip_ioctl_us',
             'start_to_submit_us','latency_us','drm_timestamp_offset_us','driver_wait_us','gpu_before_callback_us']
    for stage,rows in groups.items():
        summary={k:rows[0][k] for k in ['stage','name','mode','loops','shader','layers']}
        summary.update(frames=len(rows),missed=sum(r['missed'] for r in rows),
                       fps=1000000/stat.mean(r['interval_us'] for r in rows),
                       gpu_frames=sum(bool(r['jobs']) for r in rows),
                       asynchronous_frames=sum(r['gpu_after_submit'] for r in rows),
                       overlap_frames=sum(r['cpu_overlaps_previous_gpu'] for r in rows))
        for metric in metrics:
            values=[r[metric] for r in rows if r[metric] is not None]
            for suffix,f in [('mean',stat.mean),('p50',stat.median),('p95',lambda v:percentile(v,.95)),('max',max)]:
                summary[metric+'_'+suffix]=f(values) if values else None
        summaries.append(summary)
    with (directory/'summary.csv').open('w',newline='') as out:
        writer=csv.DictWriter(out,fieldnames=list(summaries[0]));writer.writeheader();writer.writerows(summaries)
    validation={'period_us':period,'frames':len(frames),'gpu_jobs':len(jobs),'unmatched_frames':
                [r['id'] for r in joined if not r['jobs']], 'gpu_completed_after_callback':
                [r['id'] for r in joined if r['gpu_before_callback_us'] is not None and r['gpu_before_callback_us']<0]}
    metadata=json.loads((directory/'metadata.json').read_text())
    validation['trace_overruns']={k:int(re.search(r'^overrun: (\d+)',v,re.M)[1]) for k,v in metadata['trace_stats'].items()}
    (directory/'validation.json').write_text(json.dumps(validation,indent=2))
    print('stage name mode loops shader layers CPUms GPUms FPS missed async overlap')
    for s in summaries:
        gpu=s['gpu_us_mean']/1000 if s['gpu_us_mean'] is not None else float('nan')
        print(f"{s['stage']:2} {s['name']:15} {s['mode']} {s['loops']:8} {s['shader']:3} {s['layers']:2} "
              f"{s['cpu_us_mean']/1000:6.2f} {gpu:6.2f} {s['fps']:5.1f} {s['missed']:4} {s['asynchronous_frames']:3} {s['overlap_frames']:3}")
    print(json.dumps(validation))
    if (validation['unmatched_frames'] or validation['gpu_completed_after_callback'] or
            any(validation['trace_overruns'].values())):
        raise ValueError('Capture validation failed; do not use these summaries as reliable timings')


if __name__=='__main__':
    analyse(sys.argv[1])
