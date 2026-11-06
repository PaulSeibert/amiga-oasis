#!/usr/bin/env python3
"""Make Oasis's viewer test files: IFF ILBM pictures, IFF 8SVX sounds and text files.

    mkiff.py OUTDIR

Writes the files plus OUTDIR/expect.txt: for each picture the size, planes and the checksum
of its colour planes as "oasis CMD ILBM" computes it (h = h*31 + byte over every plane in
turn, 32 bits), so the Amiga's decoding can be checked byte for byte.
"""
import math
import os
import struct
import sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
here = os.path.dirname(os.path.abspath(__file__))
expect = []


def chunk(cid, data):
    return cid + struct.pack('>I', len(data)) + data + (b'\0' if len(data) & 1 else b'')


def form(kind, chunks):
    body = kind + b''.join(chunks)
    return b'FORM' + struct.pack('>I', len(body)) + body


def byterun1(row):
    """the usual ByteRun1 packer: runs of 3+ equal bytes, literals otherwise"""
    o, i, n = bytearray(), 0, len(row)
    while i < n:
        j = i
        while j < n and j - i < 128 and row[j] == row[i]:
            j += 1
        if j - i >= 3:
            o += bytes([(257 - (j - i)) & 0xFF, row[i]])
            i = j
            continue
        j = i
        while j < n and j - i < 128 and not (j + 2 < n and row[j] == row[j + 1] == row[j + 2]):
            j += 1
        o += bytes([j - i - 1]) + row[i:j]
        i = j
    return bytes(o)


def planar(pix, w, h, depth):
    """pix[y][x] colour numbers -> list of planes, each h rows of rowbytes"""
    rb = (w + 15) // 16 * 2
    planes = []
    for p in range(depth):
        pl = bytearray(rb * h)
        for y in range(h):
            for x in range(w):
                if (pix[y][x] >> p) & 1:
                    pl[y * rb + x // 8] |= 0x80 >> (x % 8)
        planes.append(pl)
    return planes, rb


def ilbm(name, pix, w, h, depth, cmap, camg=None, compress=True, mask=False):
    planes, rb = planar(pix, w, h, depth)
    body = bytearray()
    for y in range(h):
        rows = [pl[y * rb:(y + 1) * rb] for pl in planes]
        if mask:
            rows.append(bytes([0xFF]) * rb)
        for r in rows:
            body += byterun1(bytes(r)) if compress else r
    bmhd = struct.pack('>HHhhBBBBHBBhh', w, h, 0, 0, depth, 1 if mask else 0, 1 if compress else 0,
                       0, 0, 10, 11, w, h)
    chunks = [chunk(b'BMHD', bmhd), chunk(b'CMAP', bytes(c for rgb in cmap for c in rgb))]
    if camg is not None:
        chunks.append(chunk(b'CAMG', struct.pack('>I', camg)))
    chunks.append(chunk(b'BODY', bytes(body)))
    open(os.path.join(out, name), 'wb').write(form(b'ILBM', chunks))
    hsum = 0
    for pl in planes:
        for b in pl:
            hsum = (hsum * 31 + b) & 0xFFFFFFFF
    expect.append(f'{name} {w} {h} {depth} {hsum:x}')


def logo():
    rows = [r.rstrip('\n') for r in open(os.path.join(here, '..', 'art', 'logo.txt'))]
    return [r for r in rows if r and not r.startswith('#')]


# 1. lores 320x256, 32 colours, packed: colour bars, the logo doubled in the middle
pal32 = [(0, 0, 0)] + [(int(127 + 127 * math.sin(i / 31 * 6.28)), int(127 + 127 * math.sin(i / 31 * 6.28 + 2.1)),
                        int(127 + 127 * math.sin(i / 31 * 6.28 + 4.2))) for i in range(1, 28)]
pal32 += [(0x00, 0x55, 0xAA), (0xFF, 0xFF, 0xFF), (0, 0, 0), (0xFF, 0x88, 0x00)]   # logo pens 28-31
pix = [[(x // 12) % 28 if y < 40 or y > 215 else 0 for x in range(320)] for y in range(256)]
art = logo()
pen = {'.': 28, 'W': 29, 'K': 30, 'O': 31}
for y, row in enumerate(art):
    for x, c in enumerate(row):
        for dy in (0, 1):
            for dx in (0, 1):
                pix[76 + y * 2 + dy][64 + x * 2 + dx] = pen[c]
ilbm('lores32.iff', pix, 320, 256, 5, pal32, camg=0x0000)

# 2. hires 640x200, 16 colours, not packed: diagonal stripes
pal16 = [(i * 17, 255 - i * 17, (i * 40) % 256) for i in range(16)]
pix = [[((x + y) // 20) % 16 for x in range(640)] for y in range(200)]
ilbm('hires16.iff', pix, 640, 200, 4, pal16, camg=0x8000, compress=False)

# 3. HAM 320x200: modify-blue/red/green steps across a base colour
pal = [(i * 16, i * 16, i * 16) for i in range(16)]
pix = []
for y in range(200):
    r = []
    for x in range(320):
        if x % 32 == 0:
            r.append(y // 13)                         # set from the palette
        else:
            r.append((0x20 if (x // 32) % 3 == 0 else 0x30 if (x // 32) % 3 == 1 else 0x10) | ((x % 32) // 2))
    pix.append(r)
ilbm('ham.iff', pix, 320, 200, 6, pal, camg=0x0800)

# 4. Extra Halfbrite 320x200, 64 colours
pix = [[(x // 5) % 64 for x in range(320)] for y in range(200)]
ilbm('ehb.iff', pix, 320, 200, 6, pal32, camg=0x0080)

# 5. no CAMG, a mask plane, an odd width (100 pixels: rows are padded to 112)
pal8 = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255), (255, 255, 255)]
pix = [[(x // 13 + y // 9) % 8 for x in range(100)] for y in range(60)]
ilbm('masked.iff', pix, 100, 60, 3, pal8, camg=None, mask=True)

# 6. 256 colours: needs AGA, Oasis must refuse it on screen (but still decodes it in CMD)
pix = [[(x + y) % 256 for x in range(64)] for y in range(32)]
ilbm('aga256.iff', pix, 64, 32, 8, [(i, i, i) for i in range(256)], camg=0)


# ---- sounds ----
def svx(name, data, rate, vol=0x8000, comp=0, stereo=False, samples=None):
    n = samples if samples is not None else (len(data) // 2 if stereo else len(data))
    vhdr = struct.pack('>IIIHBBI', n, 0, 32, rate, 1, comp, vol)
    chunks = [chunk(b'VHDR', vhdr), chunk(b'NAME', name.encode())]
    if stereo:
        chunks.append(chunk(b'CHAN', struct.pack('>I', 6)))
    chunks.append(chunk(b'BODY', data))
    open(os.path.join(out, name), 'wb').write(form(b'8SVX', chunks))


def tone(freq, rate, secs, amp=60):
    return bytes((int(amp * math.sin(2 * math.pi * freq * i / rate)) & 0xFF) for i in range(int(rate * secs)))


FIB = [-34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21]


def fibpack(data):
    """Fibonacci-delta: pad, first value, then the nearest delta code per sample"""
    vals = [b - 256 if b > 127 else b for b in data]
    o = bytearray([0, data[0]])
    x = vals[0]
    codes = []
    for v in vals:
        best = min(range(16), key=lambda c: abs(x + FIB[c] - v) if -128 <= x + FIB[c] <= 127 else 999)
        x += FIB[best]
        codes.append(best)
    if len(codes) & 1:
        codes.append(8)
    for i in range(0, len(codes), 2):
        o.append(codes[i] << 4 | codes[i + 1])
    return bytes(o)


t = tone(440, 8363, 1.5)
svx('tone.8svx', t, 8363)                                # 1.5 s: streams through several buffers
packed = fibpack(t)
svx('tonefib.8svx', packed, 8363, comp=1, samples=2 * (len(packed) - 2))
left, right = tone(330, 11025, 0.8), tone(495, 11025, 0.8)
svx('stereo.8svx', left + right, 11025, stereo=True)
open(os.path.join(out, 'notsound.8svx'), 'wb').write(form(b'8SVX', [chunk(b'NAME', b'no header')]))

# ---- text ----
txt = []
txt.append('Oasis text viewer test')
txt.append('')
txt.append('Tabs:\tone\ttwo\tthree')
txt.append('\x1b[1mBold\x1b[0m and \x9b33mcoloured\x9b0m text (the codes are hidden)')
txt.append('A long line: ' + ' '.join(f'word{i}' for i in range(40)))
txt += [f'Line {i:3}: the quick brown fox jumps over the lazy dog.' for i in range(1, 201)]
txt.append('Latin-1: caf\xe9 na\xefve \xc5ngstr\xf6m \xa9 \xbd')
txt.append('The end.')
open(os.path.join(out, 'readme.txt'), 'wb').write(('\r\n'.join(txt[:10]) + '\r\n' + '\n'.join(txt[10:]) + '\n').encode('latin-1'))
open(os.path.join(out, 'empty.txt'), 'wb').write(b'')
open(os.path.join(out, 'data.bin'), 'wb').write(bytes(range(256)) * 3)
open(os.path.join(out, 'expect.txt'), 'w').write('\n'.join(expect) + '\n')
print('\n'.join(expect))
