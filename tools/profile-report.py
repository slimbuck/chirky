#!/usr/bin/env python3
"""Summarise live frame captures with nested CPU costs and whole-frame GPU times."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path


def percentile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)]


def analyse(data):
    if data.get('format') != 1 or data.get('valid') is not True:
        raise ValueError('Incomplete, overflowed or unbalanced capture; timings cannot be trusted')
    count = data['frames']
    if count < 1 or count != data['target']:
        raise ValueError('Missing frames')
    frames = defaultdict(list)
    totals = defaultdict(lambda: {'cpu': [0] * count, 'self': [0] * count,
                                 'wall': [0] * count, 'rects': [0] * count, 'calls': 0})
    positions = {}
    for position, event in enumerate(data['traceEvents']):
        if event.get('ph') != 'X' or event.get('cat') != 'cpu':
            continue
        args = event['args']
        frame = args['frame']
        if not 0 <= frame < count or min(event['ts'], event['dur'], args['cpu_us'], args['rectangles']) < 0:
            raise ValueError('Invalid span')
        frames[frame].append(event)
        positions[id(event)] = position
    worst = []
    for index in range(count):
        events = frames[index]
        if not events or events[0]['name'] != 'frame' or sum(e['name'] == 'frame' for e in events) != 1:
            raise ValueError('Missing or duplicate frame root')
        children = defaultdict(int)
        local = {positions[id(event)]: i for i, event in enumerate(events)}
        for i, event in enumerate(events):
            end = event['ts'] + event['dur']
            parent_id = event['args']['parent']
            if i:
                parent = local.get(parent_id)
                if parent is None or parent >= i:
                    raise ValueError('Invalid parent scope')
                if event['ts'] < events[parent]['ts'] or end > events[parent]['ts'] + events[parent]['dur']:
                    raise ValueError('Crossing scopes')
                children[parent] += event['args']['cpu_us']
            elif parent_id != 4294967295:
                raise ValueError('Invalid frame root parent')
        contributions = defaultdict(int)
        for i, event in enumerate(events):
            name = event['name']
            cpu = event['args']['cpu_us']
            own = max(0, cpu - children[i])
            row = totals[name]
            row['cpu'][index] += cpu
            row['self'][index] += own
            row['wall'][index] += event['dur']
            row['rects'][index] += event['args']['rectangles']
            row['calls'] += 1
            contributions[name] += own
        root = events[0]
        gpu = root['args'].get('gpu_us')
        if gpu is not None and (not isinstance(gpu, int) or gpu < 0):
            raise ValueError('Invalid GPU duration')
        worst.append({'frame': index, 'elapsed_us': root['dur'],
                      'cpu_us': root['args']['cpu_us'],
                      'gpu_us': gpu,
                      'missed': root['args']['missed'], 'interval_us': root['args']['interval_us'],
                      'scene': next((e['name'] for e in events if e['name'].startswith('scene.')), 'host/other game'),
                      'top_self_cpu': sorted(contributions.items(), key=lambda pair: pair[1], reverse=True)[:3]})
    if sum(f['missed'] for f in worst) != data['missed']:
        raise ValueError('Missed-refresh total disagrees with frames')
    rows = []
    for name, values in totals.items():
        rows.append({'name': name, 'calls': values['calls'],
                     'mean_cpu_us': sum(values['cpu']) / count,
                     'mean_self_cpu_us': sum(values['self']) / count,
                     'p95_cpu_us': percentile(values['cpu'], .95),
                     'max_cpu_us': max(values['cpu']),
                     'p95_elapsed_us': percentile(values['wall'], .95),
                     'mean_rectangles': sum(values['rects']) / count})
    gpu_values = [frame['gpu_us'] for frame in worst if frame['gpu_us'] is not None]
    return {'frames': count, 'missed': data['missed'], 'budget_us': data['budget_us'],
            'mean_sprites': data['sprites'] / count if 'sprites' in data else None,
            'mean_batches': data['batches'] / count if 'batches' in data else None,
            'gpu_attribution': data.get('gpu_attribution', 'unavailable'),
            'gpu_valid_frames': len(gpu_values),
            'gpu_mean_us': sum(gpu_values) / len(gpu_values) if gpu_values else None,
            'gpu_p95_us': percentile(gpu_values, .95) if gpu_values else None,
            'scopes': sorted(rows, key=lambda row: row['mean_self_cpu_us'], reverse=True),
            'worst_frames': sorted(worst, key=lambda f: (f['missed'], f['elapsed_us'], f['cpu_us']), reverse=True)[:10],
            'cpu_heaviest_frames': sorted(worst, key=lambda f: f['cpu_us'], reverse=True)[:10]}


def report(summary):
    print(f"{summary['frames']} frames; {summary['missed']} missed refreshes; budget {summary['budget_us']/1000:.3f} ms")
    if summary['mean_sprites'] is not None:
        print(f"Draws per frame: {summary['mean_sprites']:.1f} sprites; {summary['mean_batches']:.1f} batches.")
    print('Times in ms; CPU inclusive/self, elapsed p95 and rectangles are per frame (absent scopes count as zero).')
    print('Inclusive rows overlap. Self CPU excludes nested scopes. Elapsed includes waits/preemption.')
    print(f"GPU: {summary['gpu_attribution']}; {summary['gpu_valid_frames']}/{summary['frames']} valid frames. "
          'Missing samples are unknown, not zero. GPU timings cover the whole frame, not CPU scopes.')
    if summary['gpu_valid_frames']:
        print(f"GPU mean {summary['gpu_mean_us']/1000:.3f} ms; p95 {summary['gpu_p95_us']/1000:.3f} ms (valid samples only).")
    print(f"{'Scope':26} {'CPU mean':>9} {'self mean':>9} {'CPU p95':>9} {'CPU max':>9} {'wall p95':>9} {'rects':>8}")
    for row in summary['scopes']:
        values = ' '.join(f'{row[key]/1000:9.3f}' for key in
                          ('mean_cpu_us', 'mean_self_cpu_us', 'p95_cpu_us', 'max_cpu_us', 'p95_elapsed_us'))
        print(f"{row['name']:26} {values} {row['mean_rectangles']:8.1f}")
    for title, key in [('Worst presentation/elapsed frames', 'worst_frames'),
                       ('Highest CPU frames', 'cpu_heaviest_frames')]:
        print(f'\n{title} (all times in ms):')
        for frame in summary[key]:
            costs = ', '.join(f'{name} {value/1000:.3f}' for name, value in frame['top_self_cpu'])
            gpu = '--' if frame['gpu_us'] is None else f"{frame['gpu_us']/1000:.3f}"
            print(f"  #{frame['frame']} {frame['scene']}: missed={frame['missed']} "
                  f"display={frame['interval_us']/1000:.3f} elapsed={frame['elapsed_us']/1000:.3f} "
                  f"CPU={frame['cpu_us']/1000:.3f} GPU={gpu}; self CPU: {costs}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--json', action='store_true', help='Print a machine-readable summary')
    args = parser.parse_args()
    try:
        summary = analyse(json.loads(args.capture.read_text()))
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f'Invalid capture: {error}\n')
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        report(summary)


if __name__ == '__main__':
    main()
