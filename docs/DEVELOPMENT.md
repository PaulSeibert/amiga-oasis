# Oasis: development notes

How Oasis is put together, what we learned writing it for Kickstart 1.3, and how it is tested.
For building and the release files, see the [README](../README.md).

## Design rules

- **V33 only**: built against the Kickstart 1.3 includes (`-mcrt=nix13 -m68000`), so nothing newer
  can be called by accident.
- **One engine, two front ends.** The window (`ui.c`) and command mode (`cmd.c`) call the same
  modules, so every feature can be tested headless with `oasis CMD ...` on a boot disk.
- **Small stacks.** 1.3 CLI stacks are 4 KB and DOS calls use a lot of the caller's stack: big
  buffers are `static` or allocated, recursive walks keep tiny frames, and big structs are never
  copied by value.
- **Never leave a mess.** Partial files are deleted when a copy fails or is stopped, a move deletes
  the original only after a complete copy, and an interrupted disk read leaves no half image.

## Modules

| File | What it does |
|---|---|
| `main.c` | Startup, the event loop |
| `ui.c` | The window: panes, drive bar, button banks, menus, info line, dialogs' callers, progress |
| `pane.c` | Reading a drawer into entries, sorting, selection, search-result lists |
| `dos13.c` | Device list, paths, dates, protection strings, error texts, `dos_packet`, `dos_setdate` |
| `ops.c` | Copy / move / delete / rename / makedir, with icons, metadata and the hooks for progress and errors |
| `req.c` | Own requesters: choose a button, enter a line, protection toggles (1.3 has no EasyRequest) |
| `tools.c`, `match.c` | Size, search, compare, protection parsing; the AmigaDOS wildcard matcher |
| `prefs.c` | `S:Oasis.prefs`: settings, custom buttons, LhA command lines, `%` expansion |
| `ftype.c` | File types from the first bytes, file loading, IFF chunk lookup |
| `view.c`, `ilbm.c`, `svx.c` | Text/hex viewer, ILBM screen, 8SVX player |
| `lha.c` | LhA header reader (levels 0/1/2) and LhA command lines |
| `adf.c` | ADF imaging through trackdisk.device |
| `cmd.c` | Command mode |

## Kickstart 1.3 lessons from Oasis

- **Dates:** 1.3 has no `SetFileDate`, but `ACTION_SET_DATE` works on the ROM filesystem, RAM: and
  WinUAE's host drive. Send it to the handler (`fl_Task` of the parent's lock) with a BSTR name.
- **Same object?** Compare locks by `fl_Key`, `fl_Volume` and `fl_Task`.
- **RAM:** is loaded from `L:Ram-Handler` on first use; bare boot disks need it, and memory checks
  must take their first reading after RAM: was touched.
- **Execute** needs `C:Run`, and its command line must end with `"\n"` (a libnix program hung on
  a trailing `""` without it). 1.3 command lines, in scripts too, hold 255 characters: longer
  lines silently never run.
- **audio.device:** `CMD_WRITE` needs `ADIOF_PERVOL`, and `SendIO` clears `io_Flags`, so Oasis
  calls the device's BEGINIO vector itself. Queued writes play back to back, so two small chip
  buffers per channel stream any length.
- **trackdisk imaging:** keep DOS off the drive with `ACTION_INHIBIT` (no `Inhibit()` before V36),
  read and write through a chip RAM track buffer, write with `TD_FORMAT`, and `CMD_CLEAR` before a
  verify read, or trackdisk returns its cached track. Uninhibiting makes DOS read the new disk.
- **Picture screens:** hide the pointer with a blank sprite in chip RAM, or it takes its colours
  from the picture.
- Kickstart 1.3 can't read FFS floppies without `L:FastFileSystem`.

## Tests

Each area has a boot disk script and a checker. The script makes fixtures in a host folder
mounted as `Work:`, builds a disk whose startup-sequence runs `oasis >Work:x-step.txt CMD ...`
lines, and the checker compares the results on the PC.

| Script | Checker | Checks |
|---|---|---|
| `testops.sh` | `tools/checkops.py` | file operations across Work:, RAM: and a floppy: trees, sizes, protection, comments, dates, bytes |
| `testtools.sh` | `tools/checktools.py` | size, search, wildcards, protect, comment, compare, prefs, custom buttons |
| `testview.sh` | `tools/checkview.py` | file types, text layout, decoded ILBM planes against the generator, 8SVX playing times |
| `testlha.sh` | `tools/checklha.py` | archives from LHa for UNIX (levels 0/1/2), LhA command lines via `tools/lhastub.c` |
| `testadf.sh` | `tools/checkadf.py` | ADF write/read on emulated drives (writable, write-protected, empty), byte for byte |
| `testgui.sh N` | screenshots | builds that open one dialog or view at start |

- `tools/testmatch.c` unit-tests the wildcard matcher on the host; `tools/mkiff.py` generates the
  test pictures and sounds.
- Memory is measured at the start, after one round and after a second round; the second round
  must not lose memory (both Kickstarts allocate a little on first use of devices and handlers).
- The test disks borrow `Echo`, `FailAt`, `Run`, `Wait`, `Rename` and `L:Ram-Handler` from your
  own Workbench 1.3 image (`WB13_ADF`); no Commodore files are in this repository.
- Every suite passes on Kickstart 1.3 (A500, 512K + 512K) and 3.1 (ECS, 1MB + 1MB) under WinUAE.
