#!/usr/bin/env python3
"""Check the results of testview.sh (the Oasis milestone 4 viewer test boot disk).

    checkview.py [SHARE]      SHARE = the emulator's Work: folder (default /mnt/j/amiga/share)
"""
import os
import sys

share = sys.argv[1] if len(sys.argv) > 1 else '/mnt/j/amiga/share'
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


# ---- TYPE ----
want = ['Work:vt/lores32.iff: ILBM picture', 'Work:vt/tone.8svx: 8SVX sound', 'Work:vt/readme.txt: Text',
        'Work:vt/empty.txt: Empty', 'Work:vt/data.bin: Data', 'DF0:oasis: Program', 'Work:oasis.info: Icon',
        'Work:oasis.adf: ADF disk image', 'Work:vt/nothing: (cannot read)']
got = lines('vt-type.txt')
check(got == want, 'TYPE detects pictures, sounds, text, empty, data, programs, icons, ADFs',
      [f'{g!r} != {w!r}' for g, w in zip(got + [''] * 9, want) if g != w])

# ---- ILBM: every decoded plane byte-identical to what the generator encoded ----
expect = {}
for l in open(os.path.join(share, 'vt', 'expect.txt')):
    name, w, h, d, cs = l.split()
    expect[name] = (int(w), int(h), int(d), cs)
for step, name, screen in (('vt-i1.txt', 'lores32.iff', 'screen: 320 x 256 x 5, modes $0'),
                           ('vt-i2.txt', 'hires16.iff', 'screen: 640 x 200 x 4, modes $8000'),
                           ('vt-i3.txt', 'ham.iff', 'screen: 320 x 200 x 6, modes $800'),
                           ('vt-i4.txt', 'ehb.iff', 'screen: 320 x 200 x 6, modes $80'),
                           ('vt-i5.txt', 'masked.iff', 'screen: 320 x 60 x 3, modes $0'),
                           ('vt-i6.txt', 'aga256.iff', 'screen: cannot show (Pictures with more than 64 colours need the AGA chipset.)')):
    w, h, d, cs = expect[name]
    got = lines(step)
    ok = (len(got) == 3 and got[0].startswith(f'{w} x {h}, {d} planes') and got[1] == screen
          and got[2].lower() == f'planes checksum {cs}')
    check(ok, f'ILBM {name}: {w}x{h}x{d}, decoded planes match', got)
check('masking 1' in (read('vt-i5.txt') or '') and 'CAMG $0 (none)' in (read('vt-i5.txt') or ''),
      'ILBM with a mask plane and no CAMG', lines('vt-i5.txt'))
check('not an ILBM' in (read('vt-i7.txt') or ''), 'ILBM refuses a sound file', lines('vt-i7.txt'))

# ---- LINES: the text viewer's layout ----
def escape(d, i):
    """length of an ANSI/CSI sequence at d[i], as the viewer skips them"""
    j = i
    if d[j] == 0x9B:
        j += 1
    elif d[j] == 27 and j + 1 < len(d) and d[j + 1] == ord('['):
        j += 2
    else:
        return 0
    while j < len(d) and 0x20 <= d[j] < 0x40:
        j += 1
    if j < len(d) and 0x40 <= d[j] < 0x7F:
        j += 1
    return j - i


def shown(c):
    return c >= 32 and (c < 0x7F or c >= 0xA0)


def layout(d, cols):
    """the viewer's screen lines: (start, stop) byte ranges (long lines wrap, tabs to 8)"""
    starts, i = [], 0
    while i < len(d):
        starts.append(i)
        col = 0
        while i < len(d):
            c = d[i]
            if c == 10:
                i += 1
                break
            e = escape(d, i)
            if e:
                i += e
                continue
            if c == 9:
                nc = (col // 8 + 1) * 8
                if nc > cols:
                    break
                col = nc
            elif shown(c):
                if col == cols:
                    break
                col += 1
            i += 1
    return [(s, starts[k + 1] if k + 1 < len(starts) else len(d)) for k, s in enumerate(starts)] or [(0, 0)]


def render(d, s, stop, cols):
    o, i = '', s
    while i < stop and len(o) < cols:
        e = escape(d, i)
        if e:
            i += e
            continue
        if d[i] == 9:
            o += ' ' * ((len(o) // 8 + 1) * 8 - len(o))
        elif shown(d[i]):
            o += chr(d[i])
        i += 1
    return o[:cols].rstrip(' ') + '|'


data = open(os.path.join(share, 'vt', 'readme.txt'), 'rb').read()
for step, cols, show in (('vt-l1.txt', 40, 8), ('vt-l2.txt', 76, 3)):
    lay = layout(data, cols)
    want = [f'{len(lay)} lines of {cols}'] + [render(data, s, e, cols) for s, e in lay[:show]]
    got = (read(step) or '').splitlines()
    check(got == want, f'LINES {cols}: {len(lay)} lines; tabs, ANSI codes, CR LF, wrapping as expected',
          [f'{g!r} != {w!r}' for g, w in zip(got + [''] * len(want), want) if g != w][:4])
check(len(layout(data, 40)) > 210 and 'Tabs:   one     two     three|' in (read('vt-l1.txt') or ''),
      'LINES 40: tab stops every 8 columns', '')
check(lines('vt-l3.txt') == ['1 lines of 76', '|'],'LINES of an empty file', lines('vt-l3.txt'))

# ---- PLAY: plays for as long as the sound lasts ----
for step, rate, samples, extra in (('vt-p1.txt', 8363, 12544, ''), ('vt-p2.txt', 8363, 12544, ''),
                                   ('vt-p3.txt', 11025, 8820, ' (stereo)')):
    got = lines(step)
    ticks = int(got[1].split()[2]) if len(got) > 1 and got[1].startswith('played in') else -1
    want_t = samples / rate * 50
    ok = len(got) == 2 and got[0].startswith(f'{rate} Hz, {samples} samples{extra}') and abs(ticks - want_t) <= 8
    check(ok, f'PLAY {step}: {samples} samples at {rate} Hz in about {want_t:.0f} ticks', got)
check('no VHDR' in (read('vt-p4.txt') or ''), 'PLAY refuses an 8SVX without a header', lines('vt-p4.txt'))
check('not an 8SVX' in (read('vt-p5.txt') or ''), 'PLAY refuses a picture', lines('vt-p5.txt'))


def mem(name):
    t = read(name)
    return dict(zip(t.split()[::2], map(int, t.split()[1::2]))) if t else None


m1, m2, m3 = mem('vt-mem1.txt'), mem('vt-mem2.txt'), mem('vt-mem3.txt')
if m1 and m2 and m3:
    print(f"INFO free memory: start {m1['total']}, after the first round {m2['total']} "
          f"(first use of the devices: {m1['total'] - m2['total']} bytes), after the second {m3['total']}")
    check(m1['chip'] - m3['chip'] < 512, 'no chip memory lost (the audio buffers are given back)',
          m1['chip'] - m3['chip'])
    check(m2['total'] - m3['total'] < 256, 'nothing lost on a second round (pictures, sounds, text)',
          m2['total'] - m3['total'])
check(read('vt-done.txt') is not None, 'test script ran to the end')
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
