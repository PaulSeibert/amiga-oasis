#!/usr/bin/env python3
"""Check the results of testops.sh (the Oasis file-operation test boot disk).

    checkops.py [SHARE]      SHARE = the emulator's Work: folder (default /mnt/j/amiga/share)

Reads Work:ot-*.txt written by "oasis CMD ..." on the Amiga and the copies left in
Work:ot/, and prints PASS/FAIL per check. Exit status 1 if anything failed.
"""
import os
import sys

share = sys.argv[1] if len(sys.argv) > 1 else '/mnt/j/amiga/share'
fails = 0


def read(name):
    p = os.path.join(share, name)
    return open(p, encoding='latin-1').read() if os.path.exists(p) else None


def tree(name, strip=''):
    """TREE output -> {relpath: (size, prot, date, comment)}"""
    t = read(name)
    if t is None:
        return None
    out = {}
    for line in t.splitlines():
        if line.startswith('oasis:'):
            continue
        parts = line.split('|')
        if len(parts) != 5:
            continue
        rel, size, prot, date, comment = parts
        if strip:
            if not rel.startswith(strip):
                continue
            rel = rel[len(strip):]
        out[rel.lower()] = (size, prot, date, comment)
    return out


def check(ok, what, detail=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + what + (('  -- ' + detail) if detail and not ok else ''))
    if not ok:
        fails += 1


def same(a, b, what, dates):
    """compare two trees: names, sizes, protection, comments (+ dates if asked)"""
    if not a or not b:
        check(False, what, 'missing or empty output')
        return
    diffs = []
    for k in sorted(set(a) | set(b)):
        if k not in a or k not in b:
            diffs.append(('missing ' if k not in b else 'extra ') + k)
            continue
        sa, sb = a[k], b[k]
        for i, field in enumerate(('size', 'protection', 'date', 'comment')):
            if field == 'date' and not dates:
                continue
            if sa[i] != sb[i]:
                diffs.append(f'{k} {field} {sa[i]!r} != {sb[i]!r}')
    check(not diffs, what, '; '.join(diffs[:6]))


def dates_kept(step):
    t = read(step) or ''
    for line in t.splitlines():
        if line.startswith('Dates kept:'):
            kept, lost = line[len('Dates kept: '):].rstrip('.').split(', not kept: ')
            return int(kept), int(lost)
    return None


src = tree('ot-t-src.txt')
check(src is not None and len(src) == 12, 'fixture listed (12 entries)', str(src and len(src)))
check(src and src.get('readme.txt', ('',) * 4)[3] == 'Read me first', 'comment set on readme.txt')
check(src and src.get('locked.txt', ('', ''))[1] == '----rw--', 'locked.txt is rw only',
      str(src and src.get('locked.txt')))

for step, label in (('ot-s01.txt', 'Work: -> RAM:'), ('ot-s03.txt', 'RAM: -> Work:'),
                    ('ot-s04.txt', 'Work: -> DF0: (OFS)'), ('ot-s06.txt', 'DF0: -> Work:')):
    dk = dates_kept(step)
    print(f'INFO dates {label}: ' + (f'kept {dk[0]}, not kept {dk[1]}' if dk else 'no summary'))
    t = read(step) or ''
    check('Copied 10 files and 3 drawers' in t, f'copy {label}: 10 files, 3 drawers', t.strip()[:120])

ram_dates = (dates_kept('ot-s01.txt') or (0, 1))[1] == 0
same(src, tree('ot-t-ram.txt'), 'RAM:src matches the source', ram_dates)
same(src, tree('ot-t-back.txt'), 'Work:ot/back/src matches (via RAM:)',
     ram_dates and (dates_kept('ot-s03.txt') or (0, 1))[1] == 0)
df0_dates = (dates_kept('ot-s04.txt') or (0, 1))[1] == 0
same(src, tree('ot-t-df0.txt'), 'DF0:src matches the source', df0_dates)
same(src, tree('ot-t-backdf.txt'), 'Work:ot/backdf/src matches (via DF0:)', df0_dates)

icons = tree('ot-t-icons.txt')
check(icons is not None and sorted(icons) == ['tool', 'tool.info'], 'icon travels with a copied file',
      str(icons and sorted(icons)))
check('into itself' in (read('ot-s09.txt') or ''), 'copying a drawer into itself is refused',
      (read('ot-s09.txt') or '').strip())
check('Copied 1 files' in (read('ot-s10.txt') or ''), 'existing file replaced',
      (read('ot-s10.txt') or '').strip())

same(src, tree('ot-t-moved.txt', 'src/'), 'move on the same volume (rename) keeps everything', True)
check(tree('ot-t-moved.txt') is not None and 'src' in tree('ot-t-moved.txt'), 'moved drawer arrived')
check(tree('ot-t-backafter.txt') == {}, 'moved drawer left its old place', str(tree('ot-t-backafter.txt')))
same(src, tree('ot-t-moved2.txt', 'src/'), 'move across volumes (copy + delete)', ram_dates)
ramafter = tree('ot-t-ramafter.txt') or {}
left = sorted(k for k in ramafter if k == 'src' or k.startswith('src/'))
check(left == ['src', 'src/locked.txt'], 'move left only the delete-protected file in RAM:src', str(left))
check('Protected from deletion' in (read('ot-s14.txt') or ''), 'move reported the protected file',
      (read('ot-s14.txt') or '').strip())
check(sorted(tree('ot-t-renamed.txt') or {}) == ['hammer', 'hammer.info'], 'rename takes the icon along',
      str(sorted(tree('ot-t-renamed.txt') or {})))

s16 = read('ot-s16.txt') or ''
check('Protected from deletion' in s16 and 'locked.txt' in s16, 'protected file refused without FORCE', s16.strip())
check(sorted(tree('ot-t-df0b.txt') or {}) == ['locked.txt'], 'only the protected file stayed',
      str(sorted(tree('ot-t-df0b.txt') or {})))
df0c = read('ot-t-df0c.txt') or ''
check(df0c and not any(l.split()[0].lower() == 'src' for l in df0c.splitlines() if l.split()),
      'FORCE deleted the rest', df0c)

final = tree('ot-t-final.txt') or {}
tops = sorted({k.split('/')[0] for k in final})
check(tops == ['backdf', 'moved2', 'src'], 'clean-up left exactly backdf, moved2, src', str(tops))
ramfinal = tree('ot-t-ramfinal.txt') or {}
check(not any(k.split('/')[0] in ('src', 'icons') for k in ramfinal), 'RAM: cleaned up', str(sorted(ramfinal)))

# file contents, compared on the host
base = os.path.join(share, 'ot')
bad = []
n = 0
for copy in ('moved2/src', 'backdf/src'):
    for dirpath, _, files in os.walk(os.path.join(base, 'src')):
        for f in files:
            if f.startswith('_UAEFSDB'):
                continue
            rel = os.path.relpath(os.path.join(dirpath, f), os.path.join(base, 'src'))
            other = os.path.join(base, copy, rel)
            n += 1
            if not os.path.exists(other) or open(other, 'rb').read() != open(os.path.join(dirpath, f), 'rb').read():
                bad.append(copy + '/' + rel)
check(n > 0 and not bad, f'file contents identical ({n} files via RAM: and DF0:)', ', '.join(bad))

m1, m2 = read('ot-mem1.txt'), read('ot-mem2.txt')
if m1 and m2:
    a = dict(zip(m1.split()[::2], map(int, m1.split()[1::2])))
    b = dict(zip(m2.split()[::2], map(int, m2.split()[1::2])))
    print(f"INFO free memory before {a['total']}, after {b['total']} (difference {a['total'] - b['total']} bytes)")
check(read('ot-done.txt') is not None, 'test script ran to the end')
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
