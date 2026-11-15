/* Play time: what xltshot writes while a game runs and XLT reads at its next start.
 * Shared by plugin/xltshot/main.c and src/history.c.
 *
 * SEPLUGINS/xltplay.bin (beside xltshot.log, on the device the plugin runs from)
 * is a list of these records, one per time a game ran for at least
 * XLTPLAY_FIRST_S seconds. xltshot appends its record then and rewrites the
 * seconds every XLTPLAY_EVERY_S seconds of play, so a crash or a power-off loses
 * at most that much. Time the PSP spends asleep isn't counted. XLT adds the records
 * to its play history (cache/history.bin) at start and deletes the file. */
#ifndef XLTPLAY_H
#define XLTPLAY_H

#define XLTPLAY_MAGIC 0x31504C58u /* "XLP1" */
#define XLTPLAY_FILE "SEPLUGINS/xltplay.bin"
#define XLTPLAY_FIRST_S 30
#define XLTPLAY_EVERY_S 30
#define XLTPLAY_MATCH_S 120

typedef struct {
    unsigned magic;
    unsigned seconds;           /* played in this session, sleep left out */
    unsigned long long started; /* RTC tick (UTC microseconds) when the game started */
    char id[16];                /* the game's ID from its disc, "" for homebrew and PS1 games */
    char path[224];             /* what ran: the ISO, the EBOOT.PBP, or disc0:/PSP_GAME/SYSDIR/EBOOT.BIN;
                                 * "" if the PSP didn't say (PS1 games): XLT credits it to the game it
                                 * launched within XLTPLAY_MATCH_S before started */
} __attribute__((packed)) XltPlayRecord; /* 256 bytes */

#endif
