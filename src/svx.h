/* Oasis - IFF 8SVX sounds, played through audio.device (mono on both sides, or stereo). */
#ifndef SVX_H
#define SVX_H
#include <exec/types.h>

struct SvxInfo {
    ULONG oneshot, repeat, rate;
    UBYTE octaves, compression;
    LONG  volume;                /* 0-64 */
    BOOL  stereo;
    LONG  samples;               /* per channel, of the first octave */
    const UBYTE *body;
    LONG  bodylen;
};

const char *svx_parse(const UBYTE *buf, LONG len, struct SvxInfo *si);

/* load and play path. poll() is called while it plays (and whenever one of sigs arrives) (FALSE = stop) with how far it got;
   *ticks = how long it played, in 1/50 s. Returns NULL, or a message for the user. */
typedef BOOL (*SvxPoll)(void *ctx, LONG done, LONG total);
const char *svx_play(const char *path, SvxPoll poll, void *ctx, ULONG sigs, struct SvxInfo *si, LONG *ticks);

#endif
