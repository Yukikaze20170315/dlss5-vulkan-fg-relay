"""Count frames that reached the screen without a DLSS-G evaluate, from a dlss5-bridge.log.

Reads the bounded [fg-transition] records (quality6-probe and later builds). Within
one record window, the game's frame ID (DLSSG.BackbufferFrameID) and the bridge's
armed counter normally grow together; a larger jump of the frame ID means frames
were presented without an FG evaluate (and so without NR). Also lists every change
of FG queue or FlipMetering.
"""
import re
import sys


def main(path):
    rows = []
    for line in open(path, encoding='utf-8', errors='replace'):
        if '[fg-transition]' not in line:
            continue
        d = dict(re.findall(r'(\S+?)=(\S+)', line))
        if d.get('idx', '').startswith('1/'):
            d['_time'] = line[:12]
            rows.append(d)
    switches = missing = 0
    prev = None
    for d in rows:
        frame = int(d['frame'], 16)
        armed = int(d['arm'].split('->')[1])
        state = (d['queue'], d['metering'])
        if prev is not None:
            pframe, parmed, pstate, ptime = prev
            gap = (frame - pframe) - (armed - parmed)
            if state != pstate:
                switches += 1
            if gap != 0 or state != pstate:
                missing += gap
                print(f'{ptime} -> {d["_time"]}  frames +{frame - pframe}  armed +{armed - parmed}  '
                      f'without evaluate {gap}  queue {pstate[0][-6:]}->{state[0][-6:]}  '
                      f'metering {pstate[1]}->{state[1]}  reset={d["reset"]}  foreground={d["foreground"]}')
        prev = (frame, armed, state, d['_time'])
    print(f'records={len(rows)} queue/metering switches={switches} frames without evaluate={missing}')


if __name__ == '__main__':
    main(sys.argv[1])
