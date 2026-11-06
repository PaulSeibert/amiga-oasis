#!/usr/bin/env python3
"""Check the results of testlha.sh (the Oasis milestone 5 LhA test boot disk).

    checklha.py [SHARE]      SHARE = the emulator's Work: folder (default /mnt/j/amiga/share)
"""
import os
import sys

share = sys.argv[1] if len(sys.argv) > 1 else '/mnt/j/amiga/share'
src = os.path.join(share, 'la', 'src')
fails = 0


def read(name):
    p = os.path.join(share, name)
    return open(p, encoding='latin-1').read() if os.path.exists(p) else None


def lines(name):
    return [l for l in (read(name) or '').splitlines() if l.strip()]


def check(ok, what, detail=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + what + (('  -- ' + str(detail)) if detail and not ok else ''))
    if not ok:
        fails += 1


def entries(name):
    """the entry lines of an LHALIST listing -> {name: original size or 'dir'}"""
    out = {}
    for l in lines(name):
        parts = l.split()
        if len(parts) >= 5 and parts[0] == '(drawer)':
            out[parts[-1].rstrip('/').lower()] = 'dir'
        elif len(parts) >= 7 and parts[0].isdigit() and parts[3].startswith('-l'):
            out[' '.join(parts[6:]).lower()] = int(parts[0])
    return out


def size(rel):
    return os.path.getsize(os.path.join(src, rel))


five = {n: size(n) for n in ('readme.txt', 'tool', 'tool.info', 'sub/a.txt', 'sub/deeper/b.bin')}
for step, label in (('la-l0.txt', 'header level 0'), ('la-l1.txt', 'header level 1'), ('la-l2.txt', 'header level 2')):
    got = entries(step)
    check(got == five, f'LHALIST {label}: 5 files with drawers in their names and the right sizes', got)
    t = read(step) or ''
    check(f'{sum(five.values())}' in t and '5 files, 0 drawers' in t, f'LHALIST {label}: totals', t[-120:])
st = entries('la-l3.txt')
check(st == {'readme.txt': size('readme.txt'), 'tool': size('tool')} and '-lh0-' in (read('la-l3.txt') or ''),
      'LHALIST stored (-lh0-) archive', st)
d = entries('la-l4.txt')
check(d.get('sub/a.txt') == size('sub/a.txt') and d.get('sub/deeper/b.bin') == size('sub/deeper/b.bin')
      and 'dir' in d.values(), 'LHALIST with drawer entries (-lhd-)', d)
cut = entries('la-l5.txt')
check(0 < len(cut) < 5 and all(five.get(k) == v for k, v in cut.items()),
      'LHALIST of a cut-off archive lists what is complete', cut)
check('cannot list' in (read('la-l6.txt') or ''), 'LHALIST refuses a file that is not an archive', lines('la-l6.txt'))
check(lines('la-l7.txt') == [f'Work:la/{n}: LhA archive' for n in ('h0.lha', 'h1.lha', 'h2.lha', 'stored.lha')],
      'TYPE recognises all of them', lines('la-l7.txt'))

log = (read('la-log.txt') or '').splitlines()
check('LhA [x] [Work:la/h1.lha] [Work:la/out/]' in log, 'LHAX: extract command, drawer ends with /',
      [l for l in log if '[x]' in l])
check('LhA [x] [Work:la/h2.lha] [Work:]' in log, 'LHAX into a volume root (no extra /)',
      [l for l in log if '[x]' in l])
check((read('la/out/stub-extracted.txt') or '').strip() == 'extracted from Work:la/h1.lha',
      'LHAX: LhA wrote into the destination', read('la/out/stub-extracted.txt'))
check('LhA [-r] [a] [Work:la/new.lha] [readme.txt] [with space.txt] [sub] [tool] [tool.info]' in log,
      'LHAA: pack command, quoted names (a space survives)', [l for l in log if 'new.lha' in l])
has = [l for l in log if l.startswith(('has ', 'missing '))]
check(all(l.startswith('has ') for l in has) and len(has) == 25,
      'LHAA runs LhA in the source drawer (every name found)', [l for l in has if l.startswith('missing')])
many = [l for l in log if 'many.lha' in l]
names = sum(l.count('[many/') for l in many)
a2 = [l for l in lines('la-a2.txt') if l.startswith('> ')]
check(len(many) >= 2 and names == 20 and all(len(l) - 2 <= 230 for l in a2),
      f'LHAA splits 20 names into {len(many)} command lines under 231 characters (1.3 limit 255)',
      [len(l) for l in a2])
arc = (read('la/many.lha') or '').split()
check(len(arc) == 20, 'LHAA: all 20 names reached the archive', len(arc))
check('needs the LhA program' in (read('la-n1.txt') or ''), 'without C:LhA: extracting says what is needed',
      lines('la-n1.txt'))
check(entries('la-n2.txt') == five, 'without C:LhA: listing still works (built in)', entries('la-n2.txt'))


def mem(name):
    t = read(name)
    return dict(zip(t.split()[::2], map(int, t.split()[1::2]))) if t else None


m1, m2 = mem('la-mem1.txt'), mem('la-mem2.txt')
if m1 and m2:
    print(f"INFO free memory before {m1['total']}, after {m2['total']} (difference {m1['total'] - m2['total']})")
    check(m1['total'] - m2['total'] < 1024, 'no memory lost', m1['total'] - m2['total'])
check(read('la-done.txt') is not None, 'test script ran to the end')
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
