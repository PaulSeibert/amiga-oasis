/* Oasis - ADF imaging. Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * DOS is kept off the drive while it is imaged (an ACTION_INHIBIT packet: 1.3 has no
 * Inhibit()), so it can't write to the disk halfway; afterwards it reads the disk again.
 * Tracks go through a chip RAM buffer (1.3's trackdisk needs one). Writing uses TD_FORMAT,
 * which writes a whole track, so an unformatted disk can take an image too; verifying reads
 * each track back past trackdisk's own buffer (CMD_CLEAR) and compares. */
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/trackdisk.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "adf.h"

static struct MsgPort *port;
static struct IOStdReq io;
static BOOL opened;
static UBYTE *buf, *vbuf;
static struct MsgPort *handler;              /* the drive's DOS handler, while inhibited */
static char why[100];

static struct MsgPort *mkport(void)
{
    struct MsgPort *p = AllocMem(sizeof(*p), MEMF_PUBLIC | MEMF_CLEAR);
    BYTE sig;
    if (!p)
        return NULL;
    if ((sig = AllocSignal(-1)) < 0) {
        FreeMem(p, sizeof(*p));
        return NULL;
    }
    p->mp_Node.ln_Type = NT_MSGPORT;
    p->mp_Flags = PA_SIGNAL;
    p->mp_SigBit = sig;
    p->mp_SigTask = FindTask(NULL);
    p->mp_MsgList.lh_Head = (struct Node *)&p->mp_MsgList.lh_Tail;
    p->mp_MsgList.lh_TailPred = (struct Node *)&p->mp_MsgList.lh_Head;
    return p;
}

static const char *tderr(int e)
{
    switch (e) {
    case TDERR_WriteProt:    return "the disk is write-protected";
    case TDERR_DiskChanged:  return "there is no disk in the drive (or it was taken out)";
    case TDERR_SeekError:    return "the drive could not find the track";
    case TDERR_NoMem:        return "not enough memory";
    case TDERR_BadUnitNum:   return "there is no such drive";
    case TDERR_BadDriveType: return "the drive is not a DD floppy drive";
    case TDERR_DriveInUse:   return "the drive is in use";
    case TDERR_TooFewSecs: case TDERR_NoSecHdr: case TDERR_BadSecPreamble: case TDERR_BadSecID:
    case TDERR_BadHdrSum: case TDERR_BadSecSum:
        return "the track can't be read (not an Amiga disk, or damaged)";
    default:                 return "the drive reported an error";
    }
}

static BOOL td_open(int unit)
{
    struct IOStdReq z = { 0 };
    io = z;
    if (!(port = mkport()))
        return FALSE;
    io.io_Message.mn_ReplyPort = port;
    io.io_Message.mn_Length = sizeof(io);
    if (OpenDevice((STRPTR)TD_NAME, unit, (struct IORequest *)&io, 0)) {
        FreeSignal(port->mp_SigBit);
        FreeMem(port, sizeof(*port));
        port = NULL;
        return FALSE;
    }
    opened = TRUE;
    return TRUE;
}

static void td_close(void)
{
    if (opened)
        CloseDevice((struct IORequest *)&io);
    if (port) {
        FreeSignal(port->mp_SigBit);
        FreeMem(port, sizeof(*port));
    }
    opened = FALSE;
    port = NULL;
}

static LONG td(UWORD cmd, APTR data, ULONG len, ULONG off)
{
    io.io_Command = cmd;
    io.io_Data = data;
    io.io_Length = len;
    io.io_Offset = off;
    io.io_Flags = 0;
    return DoIO((struct IORequest *)&io);
}

void adf_drives(BOOL present[4])
{
    int u;
    for (u = 0; u < 4; u++) {
        present[u] = td_open(u);
        td_close();
    }
}

static void volname(int unit, char *volume, int size)
{
    char dev[8];
    ULONG k;
    BPTR l;
    volume[0] = 0;
    fmt(dev, "DF%ld:", (LONG)unit);
    dos_quiet(TRUE);
    if ((l = Lock((STRPTR)dev, ACCESS_READ))) {
        char v[DEVNAME];
        if (dos_space(l, dev, &k, v))
            scopy(volume, v, size);
        UnLock(l);
    }
    dos_quiet(FALSE);
}

const char *adf_check(int unit, BOOL forwrite, char *volume, int size)
{
    const char *msg = NULL;
    if (volume)
        volname(unit, volume, size);
    if (!td_open(unit)) {
        fmt(why, "There is no drive DF%ld:.", (LONG)unit);
        return why;
    }
    td(TD_CHANGESTATE, NULL, 0, 0);
    if (io.io_Actual) {
        fmt(why, "There is no disk in DF%ld:.", (LONG)unit);
        msg = why;
    } else if (forwrite) {
        td(TD_PROTSTATUS, NULL, 0, 0);
        if (io.io_Actual) {
            fmt(why, "The disk in DF%ld: is write-protected.", (LONG)unit);
            msg = why;
        }
    }
    td_close();
    return msg;
}

/* keep DOS off the drive (TRUE), or let it look at the disk again (FALSE) */
static void inhibit(int unit, BOOL on)
{
    char dev[8];
    LONG res2;
    if (on) {
        fmt(dev, "DF%ld:", (LONG)unit);
        handler = (struct MsgPort *)DeviceProc((STRPTR)dev);
    }
    if (handler)
        dos_packet(handler, ACTION_INHIBIT, on ? DOSTRUE : DOSFALSE, 0, 0, 0, &res2);
    if (!on)
        handler = NULL;
}

/* is path on the floppy in DFunit:? (then imaging would read or overwrite itself) */
static BOOL ondrive(const char *path, int unit)
{
    char dev[8];
    struct MsgPort *h;
    BPTR l;
    BOOL same = FALSE;
    char dir[PATHMAX];
    fmt(dev, "DF%ld:", (LONG)unit);
    h = (struct MsgPort *)DeviceProc((STRPTR)dev);
    scopy(dir, path, PATHMAX);
    dos_quiet(TRUE);
    if (!(l = Lock((STRPTR)dir, ACCESS_READ)) && path_parent(dir))
        l = Lock((STRPTR)dir, ACCESS_READ);          /* a new file: its drawer */
    dos_quiet(FALSE);
    if (l) {
        same = h && ((struct FileLock *)BADDR(l))->fl_Task == h;
        UnLock(l);
    }
    return same;
}

static BOOL buffers(void)
{
    buf = AllocMem(ADF_TRACKBYTES, MEMF_CHIP);
    vbuf = AllocMem(ADF_TRACKBYTES, MEMF_CHIP);
    return buf && vbuf;
}

static void freebuffers(void)
{
    if (buf)
        FreeMem(buf, ADF_TRACKBYTES);
    if (vbuf)
        FreeMem(vbuf, ADF_TRACKBYTES);
    buf = vbuf = NULL;
}

/* start: the disk is there, DOS is kept off it, the motor runs */
static const char *begin(int unit, BOOL forwrite)
{
    const char *msg;
    if ((msg = adf_check(unit, forwrite, NULL, 0)))
        return msg;
    if (!buffers())
        return "Not enough chip memory for the track buffers.";
    inhibit(unit, TRUE);
    if (!td_open(unit)) {
        inhibit(unit, FALSE);
        fmt(why, "DF%ld: can't be used just now.", (LONG)unit);
        return why;
    }
    td(TD_MOTOR, NULL, 1, 0);
    return NULL;
}

static void end(int unit)
{
    td(CMD_UPDATE, NULL, 0, 0);
    td(CMD_CLEAR, NULL, 0, 0);                  /* forget the tracks it buffered */
    td(TD_MOTOR, NULL, 0, 0);
    td_close();
    inhibit(unit, FALSE);                       /* DOS reads the (new) disk */
}

const char *adf_read(int unit, const char *path, AdfProgress p, void *ctx, struct AdfResult *r)
{
    const char *msg = NULL;
    BPTR fh;
    int t, tries;
    r->bad = 0;
    r->firstbad = -1;
    r->aborted = FALSE;
    if (ondrive(path, unit)) {
        fmt(why, "The image can't be saved on DF%ld: itself.", (LONG)unit);
        return why;
    }
    if ((msg = begin(unit, FALSE))) {
        freebuffers();
        return msg;
    }
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) {
        end(unit);
        freebuffers();
        fmt(why, "Cannot create the image file: %s.", dos_errstr(IoErr()));
        return why;
    }
    for (t = 0; t < ADF_TRACKS; t++) {
        LONG e = 0;
        if (p && !p(ctx, ADF_READING, t)) {
            r->aborted = TRUE;
            break;
        }
        for (tries = 0; tries < 3; tries++) {
            if (tries)
                td(CMD_CLEAR, NULL, 0, 0);
            if (!(e = td(CMD_READ, buf, ADF_TRACKBYTES, (ULONG)t * ADF_TRACKBYTES)))
                break;
            if (e == TDERR_DiskChanged)
                break;
        }
        if (e == TDERR_DiskChanged) {
            fmt(why, "The disk was taken out at track %ld.", (LONG)t);
            msg = why;
            break;
        }
        if (e) {                                  /* unreadable: zeros, and say so */
            int k;
            for (k = 0; k < ADF_TRACKBYTES; k++)
                buf[k] = 0;
            if (!r->bad++)
                r->firstbad = t;
        }
        if (Write(fh, buf, ADF_TRACKBYTES) != ADF_TRACKBYTES) {
            fmt(why, "Cannot write the image file: %s.", dos_errstr(IoErr()));
            msg = why;
            break;
        }
    }
    Close(fh);
    end(unit);
    freebuffers();
    if (msg || r->aborted)
        DeleteFile((STRPTR)path);                /* no half images left behind */
    return msg;
}

const char *adf_write(const char *path, int unit, BOOL verify, AdfProgress p, void *ctx, struct AdfResult *r)
{
    const char *msg = NULL;
    BPTR fh;
    LONG size;
    int t, k, tries;
    r->bad = 0;
    r->firstbad = -1;
    r->aborted = FALSE;
    if (ondrive(path, unit)) {
        fmt(why, "The image is on the disk in DF%ld: that it would replace.", (LONG)unit);
        return why;
    }
    dos_quiet(TRUE);
    fh = Open((STRPTR)path, MODE_OLDFILE);
    dos_quiet(FALSE);
    if (!fh) {
        fmt(why, "Cannot read the image: %s.", dos_errstr(IoErr()));
        return why;
    }
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);
    if (size != ADF_SIZE) {
        Close(fh);
        return "This is not an 880K (DD) disk image: it must be 901120 bytes.";
    }
    if ((msg = begin(unit, TRUE))) {
        Close(fh);
        freebuffers();
        return msg;
    }
    for (t = 0; t < ADF_TRACKS && !msg; t++) {
        LONG e = 0;
        if (p && !p(ctx, ADF_WRITING, t)) {
            r->aborted = TRUE;
            break;
        }
        if (Read(fh, buf, ADF_TRACKBYTES) != ADF_TRACKBYTES) {
            fmt(why, "Cannot read the image at track %ld.", (LONG)t);
            msg = why;
            break;
        }
        for (tries = 0; tries < 3; tries++) {
            e = td(TD_FORMAT, buf, ADF_TRACKBYTES, (ULONG)t * ADF_TRACKBYTES);
            if (!e && verify) {
                if (p && !p(ctx, ADF_VERIFYING, t)) {
                    r->aborted = TRUE;
                    break;
                }
                td(CMD_UPDATE, NULL, 0, 0);
                td(CMD_CLEAR, NULL, 0, 0);
                if (!(e = td(CMD_READ, vbuf, ADF_TRACKBYTES, (ULONG)t * ADF_TRACKBYTES))) {
                    for (k = 0; k < ADF_TRACKBYTES && vbuf[k] == buf[k]; k++)
                        ;
                    if (k < ADF_TRACKBYTES)
                        e = -1;                     /* read back, but different */
                }
            }
            if (!e || e == TDERR_WriteProt || e == TDERR_DiskChanged)
                break;
        }
        if (r->aborted)
            break;
        if (e) {
            if (e == -1)
                fmt(why, "Track %ld did not read back the same: the disk may be worn.", (LONG)t);
            else
                fmt(why, "Writing track %ld failed: %s.", (LONG)t, tderr((int)e));
            msg = why;
        }
    }
    Close(fh);
    end(unit);
    freebuffers();
    return msg;
}
