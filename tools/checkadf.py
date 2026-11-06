#!/usr/bin/env python3
"""Check the results of testadf.sh (the Oasis milestone 6 ADF imaging test boot disk).

    checkadf.py [SHARE]      SHARE = the emulator's Work: folder (default /mnt/j/amiga/share)
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


def data(name):
    p = os.path.join(share, name)
    return open(p, 'rb').read() if os.path.exists(p) else None


def check(ok, what, detail=''):
    global fails
    print(('PASS ' if ok else 'FAIL ') + what + (('  -- ' + str(detail)) if detail and not ok else ''))
    if not ok:
        fails += 1


def names(listing):
    return sorted(l[:30].strip().lower() for l in lines(listing) if not l.startswith('oasis:'))


check(lines('ad-i0.txt') == ['drives: DF0: DF1: DF2: DF3:'], 'ADFINFO finds the four drives', lines('ad-i0.txt'))
check(lines('ad-i1.txt')[1:] == ['DF1: ready to write (volume OldDisk)'], 'DF1: ready, volume name', lines('ad-i1.txt'))
# (the disk is FFS: Kickstart 1.3 can't read FFS without L:FastFileSystem, so no volume name there)
check(lines('ad-i2.txt')[1:] in (['DF2: The disk in DF2: is write-protected. (volume Locked)'],
                                 ['DF2: The disk in DF2: is write-protected.']),
      'DF2: write-protected', lines('ad-i2.txt'))
check(lines('ad-i3.txt')[1:] == ['DF3: There is no disk in DF3:.'], 'DF3: no disk', lines('ad-i3.txt'))
check(names('ad-l1.txt') == ['old.txt'], 'DF1: starts with the old disk', names('ad-l1.txt'))

check(lines('ad-w1.txt') == ['Image written and verified.'], 'ADFWRITE ... VERIFY', lines('ad-w1.txt'))
check(names('ad-l2.txt') == ['big.bin', 'readme', 'sub'], 'DOS reads the new disk afterwards (its files listed)',
      names('ad-l2.txt'))
check('(volume Imaged)' in (read('ad-i4.txt') or ''), 'the new volume name is seen', lines('ad-i4.txt'))
test, old, prot = data('ad/test.adf'), data('ad/old.adf'), data('ad/prot.adf')
check(lines('ad-r1.txt') == ['Disk read into the image.'], 'ADFREAD DF1:', lines('ad-r1.txt'))
check(data('ad/back.adf') == test and test is not None, 'the image read back is byte-identical to the one written',
      len(data('ad/back.adf') or b''))
check(lines('ad-r2.txt') == ['Disk read into the image.'] and data('ad/prot-back.adf') == prot,
      'a write-protected disk can be read (byte-identical)', lines('ad-r2.txt'))
check('write-protected' in (read('ad-w2.txt') or ''), 'writing to a write-protected disk is refused', lines('ad-w2.txt'))
check('no disk' in (read('ad-w3.txt') or ''), 'writing to an empty drive is refused', lines('ad-w3.txt'))
check('no disk' in (read('ad-r3.txt') or '') and data('ad/none.adf') is None,
      'reading an empty drive is refused (no file left)', lines('ad-r3.txt'))
check('901120' in (read('ad-w4.txt') or ''), 'an image of the wrong size is refused', lines('ad-w4.txt'))
check('itself' in (read('ad-r4.txt') or ''), 'an image is not saved on the disk being read', lines('ad-r4.txt'))
check(lines('ad-w5.txt') == ['Image written.'] and names('ad-l3.txt') == ['old.txt'],
      'ADFWRITE without verify, and DOS sees the old disk again', lines('ad-w5.txt') + names('ad-l3.txt'))
check(data('ad/back2.adf') == old and old is not None, 'reading it back gives the old image, byte-identical')
check(lines('ad-t1.txt') == ['Work:ad/back.adf: ADF disk image', 'Work:ad/small.bin: Data'],
      'TYPE: ADF disk image', lines('ad-t1.txt'))


def mem(name):
    t = read(name)
    return dict(zip(t.split()[::2], map(int, t.split()[1::2]))) if t else None


check(data('ad/back3.adf') == test and data('ad/prot-back2.adf') == prot, 'a second round gives the same images')
m1, m2, m3 = mem('ad-mem1.txt'), mem('ad-mem2.txt'), mem('ad-mem3.txt')
if m1 and m2 and m3:
    print(f"INFO free memory: start {m1['total']}, after the first round {m2['total']} "
          f"({m1['total'] - m2['total']} bytes: the system's own first use), after the second {m3['total']}")
    check(m1['chip'] - m3['chip'] < 512, 'no chip memory lost (track buffers given back)', m1['chip'] - m3['chip'])
    check(m2['total'] - m3['total'] < 256, 'nothing lost on a second round of writing and reading',
          m2['total'] - m3['total'])
check(read('ad-done.txt') is not None, 'test script ran to the end')
print('ALL PASSED' if not fails else f'{fails} FAILED')
sys.exit(1 if fails else 0)
