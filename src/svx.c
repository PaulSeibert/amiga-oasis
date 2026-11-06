/* Oasis - 8SVX player. Written by Paul Seibert, (c) 2026 Desert Palms Software.
 *
 * The sound is loaded into any memory (Fibonacci-delta packed sounds are unpacked first) and
 * streamed to audio.device through two small chip RAM buffers per channel, so long sounds
 * play on machines with little chip RAM. A mono sound plays on a left and a right channel. */
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/audio.h>
#include <graphics/gfxbase.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "util.h"
#include "dos13.h"
#include "ftype.h"
#include "svx.h"

extern struct GfxBase *GfxBase;

#define CHUNK 8192               /* bytes per chip buffer (two per channel) */

const char *svx_parse(const UBYTE *buf, LONG len, struct SvxInfo *si)
{
    const UBYTE *d;
    LONG n;
    if (len < 12 || be32(buf) != 0x464F524D || be32(buf + 8) != 0x38535658)   /* FORM 8SVX */
        return "This is not an 8SVX sound.";
    if (!iff_chunk(buf, len, "VHDR", &d, &n) || n < 20)
        return "The sound has no VHDR header.";
    si->oneshot = be32(d);
    si->repeat = be32(d + 4);
    si->rate = be16(d + 12);
    si->octaves = d[14];
    si->compression = d[15];
    si->volume = (LONG)(be32(d + 16) >> 10);       /* Fixed 16.16, 1.0 = full: 0-64 */
    if (si->volume <= 0 || si->volume > 64)
        si->volume = 64;
    if (si->compression > 1)
        return "The sound uses a packing method Oasis doesn't know.";
    if (!si->rate)
        return "The sound has no sample rate.";
    si->stereo = iff_chunk(buf, len, "CHAN", &d, &n) && n >= 4 && be32(d) == 6;
    if (!iff_chunk(buf, len, "BODY", &si->body, &si->bodylen) || !si->bodylen)
        return "The sound has no BODY (no sample data).";
    return NULL;
}

/* Fibonacci-delta: a pad byte, the first value, then a 4-bit code per sample */
static void fibunpack(const UBYTE *s, LONG n, BYTE *d)
{
    static const BYTE delta[16] = { -34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21 };
    BYTE x = n > 1 ? (BYTE)s[1] : 0;
    LONG i;
    for (i = 2; i < n; i++) {
        x += delta[s[i] >> 4];
        *d++ = x;
        x += delta[s[i] & 15];
        *d++ = x;
    }
}

/* ---- audio.device ---------------------------------------------------------------------- */

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

static void rmport(struct MsgPort *p)
{
    if (p) {
        FreeSignal(p->mp_SigBit);
        FreeMem(p, sizeof(*p));
    }
}

/* exec's BeginIO, which keeps io_Flags (SendIO clears ADIOF_PERVOL): call the device's
   BEGINIO vector with the request in a1 and the device in a6 */
static void beginio(struct IORequest *io)
{
    register struct IORequest *a1 __asm("a1") = io;
    register struct Device *a6 __asm("a6") = io->io_Device;
    __asm volatile ("jsr -30(a6)" : : "r"(a1), "r"(a6) : "d0", "d1", "a0", "memory", "cc");
}

struct Chan {
    const BYTE *src;
    LONG left;                   /* bytes still to send */
    UBYTE *buf[2];
    struct IOAudio io[2];
    BOOL busy[2];
};

const char *svx_play(const char *path, SvxPoll poll, void *ctx, ULONG sigs, struct SvxInfo *si, LONG *ticks)
{
    static UBYTE map[] = { 3, 5, 10, 12 };            /* a left and a right channel */
    static struct IOAudio alloc;
    static struct Chan ch[2];
    static char why[80];
    struct DateStamp t0, t1;
    struct MsgPort *port = NULL;
    UBYTE *file;
    BYTE *unpacked = NULL;
    LONG len, size, falloc, err, ualloc = 0, total, sent = 0;
    const char *msg = NULL;
    ULONG clock, period;
    BOOL opened = FALSE, stop = FALSE;
    int c, b, k, nch = 0;

    *ticks = 0;
    dos_quiet(TRUE);
    file = ft_load(path, 0x7FFFFFFF, 0, &len, &size, &falloc, &err);
    dos_quiet(FALSE);
    if (!file) {
        fmt(why, "Cannot read the sound: %s.", dos_errstr(err));
        return why;
    }
    if ((msg = svx_parse(file, len, si)))
        goto out;
    {   /* the sample data of each channel */
        const BYTE *data[2];
        LONG per = si->stereo ? si->bodylen / 2 : si->bodylen, first;
        data[0] = (const BYTE *)si->body;
        data[1] = (const BYTE *)si->body + per;
        if (si->compression == 1) {
            LONG out = 2 * (per - 2);
            if (out <= 0) {
                msg = "The sound is empty.";
                goto out;
            }
            ualloc = out * (si->stereo ? 2 : 1);
            if (!(unpacked = AllocMem(ualloc, MEMF_PUBLIC))) {
                msg = "Not enough memory to unpack the sound.";
                goto out;
            }
            fibunpack(si->body, per, unpacked);
            if (si->stereo)
                fibunpack(si->body + per, per, unpacked + out);
            data[0] = unpacked;
            data[1] = unpacked + out;
            per = out;
        }
        first = si->oneshot + si->repeat;            /* only the first (highest) octave */
        si->samples = first > 0 && first <= per ? first : per;
        for (c = 0; c < 2; c++) {
            ch[c].src = data[si->stereo ? c : 0];
            ch[c].left = si->samples & ~1;           /* audio.device wants an even length */
            ch[c].busy[0] = ch[c].busy[1] = FALSE;
            ch[c].buf[0] = ch[c].buf[1] = NULL;
        }
    }
    total = si->samples & ~1;
    if (total < 2) {
        msg = "The sound is empty.";
        goto out;
    }
    for (c = 0; c < 2; c++)
        for (b = 0; b < 2; b++)
            if (!(ch[c].buf[b] = AllocMem(CHUNK, MEMF_CHIP))) {
                msg = "Not enough chip memory to play the sound.";
                goto out;
            }
    if (!(port = mkport())) {
        msg = "Not enough memory to play the sound.";
        goto out;
    }
    alloc.ioa_Request.io_Message.mn_ReplyPort = port;
    alloc.ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    alloc.ioa_Request.io_Command = ADCMD_ALLOCATE;
    alloc.ioa_Request.io_Flags = ADIOF_NOWAIT;
    alloc.ioa_AllocKey = 0;
    alloc.ioa_Data = map;
    alloc.ioa_Length = sizeof(map);
    if (OpenDevice((STRPTR)"audio.device", 0, (struct IORequest *)&alloc, 0)) {
        msg = "The sound channels are in use (or there is no audio.device).";
        goto out;
    }
    opened = TRUE;
    clock = (GfxBase->DisplayFlags & PAL) ? 3546895 : 3579545;
    period = clock / si->rate;
    if (period < 124)
        period = 124;                                /* the fastest Paula can play */
    if (period > 65535)
        period = 65535;
    /* one request per buffer, each for one of the two channels it got */
    for (k = 0, c = 0; k < 4 && c < 2; k++)
        if ((ULONG)alloc.ioa_Request.io_Unit & (1UL << k)) {
            /* channels 0 and 3 are on the left: keep left in ch[0] for stereo */
            int slot = (k == 0 || k == 3) ? 0 : 1;
            for (b = 0; b < 2; b++) {
                struct IOAudio *io = &ch[slot].io[b];
                *io = alloc;
                io->ioa_Request.io_Unit = (struct Unit *)(1UL << k);
                io->ioa_Request.io_Command = CMD_WRITE;
                io->ioa_Request.io_Flags = ADIOF_PERVOL;
                io->ioa_Period = (UWORD)period;
                io->ioa_Volume = (UWORD)si->volume;
                io->ioa_Cycles = 1;
            }
            nch++;
            c++;
        }
    DateStamp(&t0);
    /* start both buffers of both channels, then refill each as it comes back */
    for (b = 0; b < 2; b++)
        for (c = 0; c < nch; c++) {
            struct Chan *h = &ch[c];
            LONG n = h->left < CHUNK ? h->left : CHUNK;
            if (n <= 0)
                continue;
            CopyMem((APTR)h->src, h->buf[b], n);
            h->src += n;
            h->left -= n;
            h->io[b].ioa_Data = h->buf[b];
            h->io[b].ioa_Length = n;
            beginio((struct IORequest *)&h->io[b]);
            h->busy[b] = TRUE;
            if (c == 0)
                sent += n;
        }
    for (;;) {
        struct Message *m;
        BOOL any = FALSE;
        for (c = 0; c < nch; c++)
            any |= ch[c].busy[0] | ch[c].busy[1];
        if (!any)
            break;
        Wait(1UL << port->mp_SigBit | SIGBREAKF_CTRL_C | sigs);
        while ((m = GetMsg(port))) {
            for (c = 0; c < nch; c++)
                for (b = 0; b < 2; b++)
                    if (m == (struct Message *)&ch[c].io[b]) {
                        struct Chan *h = &ch[c];
                        LONG n = h->left < CHUNK ? h->left : CHUNK;
                        h->busy[b] = FALSE;
                        if (n > 0 && !stop) {
                            CopyMem((APTR)h->src, h->buf[b], n);
                            h->src += n;
                            h->left -= n;
                            h->io[b].ioa_Length = n;
                            beginio((struct IORequest *)&h->io[b]);
                            h->busy[b] = TRUE;
                            if (c == 0)
                                sent += n;
                        }
                    }
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            stop = TRUE;
        if (!stop && poll && !poll(ctx, sent, total))
            stop = TRUE;
        if (stop)                                       /* cut it short: take back what's queued */
            for (c = 0; c < nch; c++)
                for (b = 0; b < 2; b++)
                    if (ch[c].busy[b]) {
                        AbortIO((struct IORequest *)&ch[c].io[b]);
                        WaitIO((struct IORequest *)&ch[c].io[b]);
                        ch[c].busy[b] = FALSE;
                    }
    }
    DateStamp(&t1);
    *ticks = (t1.ds_Days - t0.ds_Days) * 24 * 60 * 3000 + (t1.ds_Minute - t0.ds_Minute) * 3000 +
             (t1.ds_Tick - t0.ds_Tick);
    if (stop)
        msg = "Stopped.";
out:
    if (opened)
        CloseDevice((struct IORequest *)&alloc);           /* also frees the channels */
    rmport(port);
    for (c = 0; c < 2; c++)
        for (b = 0; b < 2; b++)
            if (ch[c].buf[b]) {
                FreeMem(ch[c].buf[b], CHUNK);
                ch[c].buf[b] = NULL;
            }
    if (unpacked)
        FreeMem(unpacked, ualloc);
    FreeMem(file, falloc);
    return msg;
}
