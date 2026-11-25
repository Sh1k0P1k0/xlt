/* XLT - ARK detection and the CPU lines in ARK's SETTINGS.TXT. */
#ifndef ARK_H
#define ARK_H

enum { CLOCK_AUTO, CLOCK_133, CLOCK_222, CLOCK_333, CLOCK_COUNT };

void ark_init(void);
int ark_version(void);        /* 0 = no ARK, 4 or 5 */
const char *ark_dir(void);    /* e.g. "ms0:/PSP/SAVEDATA/ARK_01234/", "" if unknown */

/* CPU clock ARK applies when a game starts. id = game ID for a per-game line,
 * or NULL for the default for all games. ARK reads SETTINGS.TXT on every
 * launch, so a change applies to the next game started. */
int ark_get_clock(const char *id);
int ark_set_clock(const char *id, int clock); /* 0 on success */

const char *clock_label(int clock);

/* XLT's own EBOOT path (argv[0]), before ark_free_clock. */
void ark_set_self(const char *path);
/* Makes sure no ARK clock line locks XLT's clock (arktext_free_clock), writing
 * SETTINGS.TXT if needed: 1 written (applies from XLT's next start), 0 nothing
 * to do, -1 failed. File I/O: the worker. Settings writes keep it too. */
int ark_free_clock(void); /* "Auto", "133 MHz", ... */

/* The whole SETTINGS.TXT (Settings tab). Load gives ARK's defaults when the
 * file is missing; lines that aren't settings go to extra. 0 on success. */
#include "arkcfg.h"
int ark_load_cfg(ArkCfg *c, char *extra, int cap);
int ark_save_cfg(const ArkCfg *c, const char *extra);

#endif
