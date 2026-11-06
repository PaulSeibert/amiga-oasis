#!/usr/bin/env python3
"""Check the results of testtools.sh (the Oasis milestone 3 tools test boot disk).

    checktools.py [SHARE]      SHARE = the emulator's Work: folder (default /mnt/j/amiga/share)
"""
import os
import sys

share = sys.argv[1] if len(sys.argv) > 1 else '/mnt/j/amiga/share'
fails = 0


def read(name):
    p = os.path.join(share, name)
    return open(p, encoding='latin-1').read() if os.path.exists(p) else None


def check(ok, what, detail=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + what + (('  -- ' + str(detail)) if detail and not ok else ''))
    if not ok:
        fails += 1


def lines(name):
    return [l for l in (read(name) or '').splitlines() if l.strip()]


def listing(name):
    """CMD LIST output -> {name: (size, prot, comment)}"""
    out = {}
    for l in lines(name):
        if l.startswith('oasis:'):
            continue
        name_, rest = l[:30].rstrip(), l[30:].split(None, 4)
        if len(rest) >= 4:
            out[name_.lower()] = (rest[0], rest[1], rest[4] if len(rest) > 4 else '')
    return out


# ---- SIZE: compare with the host tree (skipping WinUAE's metadata files) ----
def host_size(rel):
    files = dirs = size = 0
    top = os.path.join(share, rel)
    if os.path.isfile(top):
        return 1, 0, os.path.getsize(top)
    for d, ds, fs in os.walk(top):
        dirs += len(ds)
        for f in fs:
            if f.startswith('_UAEFSDB'):
                continue
            files += 1
            size += os.path.getsize(os.path.join(d, f))
    return files, dirs, size


f, d, b = host_size('tt/src')
s1 = read('tt-size1.txt') or ''
check(s1.startswith(f'{f} files, {d} drawers, {b} bytes'), f'SIZE of the tree: {f} files, {d} drawers, {b} bytes', s1.strip())
f2, d2, b2 = host_size('tt/src/big.bin')
f3, d3, b3 = host_size('tt/src/sub')
s2 = read('tt-size2.txt') or ''
check(s2.startswith(f'{f2 + f3} files, {d3} drawers, {b2 + b3} bytes'), 'SIZE of a file and a drawer together', s2.strip())
check('cannot read' in (read('tt-size3.txt') or ''), 'SIZE of a missing path reports it', (read('tt-size3.txt') or '').strip())


# ---- SEARCH ----
def hits(name):
    ls = lines(name)
    return sorted(l.lower() for l in ls if not l.endswith('found.')), ls[-1] if ls else ''


for step, want in (('tt-s1.txt', ['name with space.txt', 'notes.txt', 'readme.txt', 'sub/a.txt', 'sub/deeper/b.txt']),
                   ('tt-s2.txt', ['name with space.txt', 'notes.txt', 'readme.txt', 'sub/a.txt', 'sub/deeper/b.txt']),
                   ('tt-s3.txt', ['big.bin', 'empty.dat', 'sub/deeper/c.bin']),
                   ('tt-s4.txt', ['sub/deeper (dir)']),
                   ('tt-s5.txt', ['tool']),
                   ('tt-s6.txt', [])):
    h, last = hits(step)
    check(h == sorted(want) and last == f'{len(want)} found.', f'SEARCH {step}: {len(want)} found', f'{h} / {last!r}')
check('cannot search' in (read('tt-s7.txt') or ''), 'SEARCH in a file is refused', (read('tt-s7.txt') or '').strip())
check(lines('tt-m1.txt') == ['yes tool.info', 'no  tool', 'yes TOOL.INFO'], 'MATCH #?.info', lines('tt-m1.txt'))
check(lines('tt-m2.txt') == ['yes ac', 'yes bccc', 'no  c', 'no  abc'], 'MATCH (a|b)#c', lines('tt-m2.txt'))

# ---- PROTECT ----
check(listing('tt-p1.txt').get('tool', ('', ''))[1] == '----rwed', 'PROTECT rwed', listing('tt-p1.txt').get('tool'))
check(listing('tt-p3.txt').get('tool', ('', ''))[1] == '-sp-rwed', 'PROTECT +sp', listing('tt-p3.txt').get('tool'))
check(listing('tt-p5.txt').get('tool', ('', ''))[1] == '-sp-r-e-', 'PROTECT -wd', listing('tt-p5.txt').get('tool'))
check('cannot protect' in (read('tt-p6.txt') or ''), 'PROTECT with bad letters is refused', (read('tt-p6.txt') or '').strip())
check(listing('tt-r4.txt').get('tool', ('', ''))[1] == '---arw-d', 'PROTECT +a then -e on RAM:', listing('tt-r4.txt').get('tool'))

# ---- COMMENT ----
check(listing('tt-c2.txt').get('readme.txt', ('',) * 3)[2] == 'Hello from Oasis', 'COMMENT set on Work:',
      listing('tt-c2.txt').get('readme.txt'))
check(listing('tt-c4.txt').get('tool', ('',) * 3)[2] == 'On the RAM disk', 'COMMENT set on RAM:',
      listing('tt-c4.txt').get('tool'))
check(listing('tt-c6.txt').get('readme.txt', ('', '', 'x'))[2] == '', 'COMMENT "" removes it',
      listing('tt-c6.txt').get('readme.txt'))

# ---- COMPARE ----
k1 = lines('tt-k1.txt')
want = ['differs: clash', 'differs: date.diff', 'differs: size.diff', 'only in 1: onlyA.txt', 'only in 2: onlyB.txt']
check(sorted(k1[:-1]) == want and k1[-1:] == ['1 only in 1, 1 only in 2, 3 differ, 3 the same.'],
      'COMPARE two drawers', k1)
k3 = lines('tt-k3.txt')
check(k3 == ['0 only in 1, 0 only in 2, 0 differ, 7 the same.'], 'COMPARE with an Oasis copy on RAM: (dates kept)', k3)
check('cannot read' in (read('tt-k4.txt') or ''), 'COMPARE with a missing drawer reports it', (read('tt-k4.txt') or '').strip())

# ---- PREFS ----
f1 = lines('tt-f1.txt')
want = ['LEFT "Work:tt/a"', 'RIGHT ""', 'SORT DATE', 'HIDEICONS YES',
        'BUTTON 1 "Args" [DF0:oasis CMD MATCH #?.txt %s]',
        'BUTTON 2 "Copy to" [DF0:oasis CMD COPY %s TO %d]',
        'BUTTON 3 "Quiet" QUIET [Echo >Work:tt-quiet.txt "quiet ran"]',
        'BUTTON 4 "Pct" [DF0:oasis CMD MATCH 100%% %p]',
        'BUTTON 5 "Here" [DF0:oasis CMD LIST ""]',
        'Not understood: 2 lines, the first is line 11.']
check(f1 == want, 'PREFS parsed (CRLF file, quoted and bare commands, QUIET, bad lines)',
      '\n'.join(f'{a!r} | {b!r}' for a, b in zip(f1 + [''] * 12, want + [''] * 12) if a != b))
check('cannot read' in (read('tt-f2.txt') or ''), 'PREFS: a missing file is reported', (read('tt-f2.txt') or '').strip())

# ---- BUTTON ----
b1 = lines('tt-b1.txt')
check(b1 == ['> DF0:oasis CMD MATCH #?.txt "Work:tt/src/readme.txt" "Work:tt/src/name with space.txt"',
             'yes Work:tt/src/readme.txt', 'yes Work:tt/src/name with space.txt'],
      'BUTTON %s: quoted full paths, one argument each', b1)
b2 = read('tt-b2.txt') or ''
check('> DF0:oasis CMD COPY "Work:tt/src/readme.txt" TO "Work:tt/dst"' in b2 and 'Copied 1 files' in b2,
      'BUTTON %s %d: copy into the destination', b2.strip())
dst = lines('tt-t-dst.txt')
check(any(l.lower().startswith('readme.txt|') for l in dst), 'the button\'s copy arrived', dst)
check((read('tt-quiet.txt') or '').strip() == 'quiet ran', 'BUTTON with output redirection', read('tt-quiet.txt'))
b4 = lines('tt-b4.txt')
check(b4[:1] == ['> DF0:oasis CMD MATCH 100% "Work:tt/src"'], 'BUTTON %% and %p', b4)
b5 = read('tt-b5.txt') or ''
check('a.txt' in b5 and 'deeper' in b5, 'BUTTON runs in the source drawer (current directory)', b5.strip())
check('needs files' in (read('tt-b6.txt') or ''), 'BUTTON with %s and nothing selected is refused', (read('tt-b6.txt') or '').strip())
check('no such button' in (read('tt-b7.txt') or ''), 'BUTTON 9 does not exist', (read('tt-b7.txt') or '').strip())

def mem(name):
    t = read(name)
    return dict(zip(t.split()[::2], map(int, t.split()[1::2])))['total'] if t else None


m = [mem(n) for n in ('tt-mem1.txt', 'tt-memA.txt', 'tt-memB.txt', 'tt-mem2.txt')]
if None not in m:
    print(f'INFO free memory: start {m[0]}, before the buttons {m[1]}, after them {m[2]}, end {m[3]}')
    print(f'INFO lost by the buttons (Execute) {m[1] - m[2]}, overall {m[0] - m[3]} bytes')
    check(m[0] - m[3] < 1024, 'no memory lost overall (under 1 KB, RAM: emptied again)', m[0] - m[3])
check(read('tt-done.txt') is not None, 'test script ran to the end')
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
