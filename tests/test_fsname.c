/* Host test of src/fsname.c: reads UTF-8 names (one per line) and prints, tab-separated,
 * the Shift-JIS bytes fsname_from_utf8 makes (hex), whether it changed anything, and those
 * bytes read back with fsname_to_utf8. tests/check_fsname.py compares with XLT Tool's naming. */
#include <stdio.h>
#include <string.h>
#include "fsname.h"

int main(void)
{
    char line[1024], fs[1024], back[2048];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;
        int changed = 0;
        int r = fsname_from_utf8(line, fs, sizeof(fs), &changed);
        for (const unsigned char *p = (const unsigned char *)fs; *p; p++)
            printf("%02x", *p);
        fsname_to_utf8(fs, back, sizeof(back));
        printf("\t%d\t%d\t%s\n", changed, r, back);
    }
    /* every two-byte Shift-JIS character decodes, and re-encodes to itself or an equal one */
    int bad = 0, n = 0;
    for (int lead = 0x81; lead <= 0xFC; lead++) {
        if (lead >= 0xA0 && lead < 0xE0) continue;
        for (int t = 0x40; t <= 0xFC; t++) {
            unsigned char s[3] = {(unsigned char)lead, (unsigned char)t, 0};
            unsigned cp;
            uint32_t c;
            if (sjis_char(s, 2, &c) != 2) continue;
            n++;
            fsname_to_utf8((char *)s, back, sizeof(back));
            int ch = 0;
            fsname_from_utf8(back, fs, sizeof(fs), &ch);
            if (ch || strlen(fs) != 2) { bad++; continue; }
            (void)cp;
        }
    }
    fprintf(stderr, "%d two-byte characters, %d don't come back as one\n", n, bad);
    return bad != 0;
}
