/* Oasis - ADF disk images: read a floppy into an .adf file, or write one onto a floppy,
 * track by track through trackdisk.device (DD disks: 80 cylinders x 2 sides x 11 sectors). */
#ifndef ADF_H
#define ADF_H
#include <exec/types.h>

#define ADF_SIZE       901120
#define ADF_TRACKS     160
#define ADF_TRACKBYTES 5632

enum { ADF_READING, ADF_WRITING, ADF_VERIFYING };

/* called before each track; FALSE = stop */
typedef BOOL (*AdfProgress)(void *ctx, int phase, int track);

struct AdfResult {
    int  bad;                    /* tracks that could not be read (written as zeros) */
    int  firstbad;
    BOOL aborted;
};

/* which of DF0:-DF3: exist */
void adf_drives(BOOL present[4]);
/* NULL if the disk in DFunit: is ready (for writing: not write-protected), else why not;
   volume gets the disk's name ("" if it has none, e.g. not a DOS disk) */
const char *adf_check(int unit, BOOL forwrite, char *volume, int size);
/* NULL, or a message for the user */
const char *adf_read(int unit, const char *path, AdfProgress p, void *ctx, struct AdfResult *r);
const char *adf_write(const char *path, int unit, BOOL verify, AdfProgress p, void *ctx, struct AdfResult *r);

#endif
