/* Host test for src/dumpname.c: ISO names for UMD dumps, the PVD size, time left. */
#include <stdio.h>
#include <string.h>
#include "dumpname.h"

static int fails;

static void name(const char *title, const char *id, const char *want)
{
    char out[256];
    dump_file_name(title, id, out, sizeof(out));
    if (strcmp(out, want)) {
        printf("FAIL name(\"%s\", \"%s\") = \"%s\", want \"%s\"\n", title ? title : "(null)", id ? id : "(null)", out, want);
        fails++;
    }
}

static void left(uint64_t done, uint64_t total, uint32_t ms, const char *want)
{
    char out[64];
    dump_time_left(done, total, ms, out, sizeof(out));
    if (strcmp(out, want)) {
        printf("FAIL left(%llu, %llu, %u) = \"%s\", want \"%s\"\n", (unsigned long long)done,
               (unsigned long long)total, ms, out, want);
        fails++;
    }
}

int main(void)
{
    name("Midnight Club 3: DUB Edition", "ULES00577", "Midnight Club 3 DUB Edition [ULES00577].iso");
    name("Ratchet & Clank: Size Matters", "UCUS98633", "Ratchet & Clank Size Matters [UCUS98633].iso");
    name("  What?  <Is> \"This\" | ok.  ", "ULUS10001", "What Is This ok [ULUS10001].iso");
    name("Tab\there\nnewline", "ULJS00001", "Tab here newline [ULJS00001].iso");
    name("...Dots...", "NPJH00001", "Dots [NPJH00001].iso");
    name("", "ULES00001", "[ULES00001].iso");
    name(NULL, "ULES00001", "[ULES00001].iso");
    name("Only a title", "", "Only a title.iso");
    name("", "", "UMD.iso");
    name("???", "", "UMD.iso");
    name("モンスターハンター", "ULJM05066", "モンスターハンター [ULJM05066].iso");
    name("Ελληνικά", "ULES00002", "Ελληνικά [ULES00002].iso");

    /* A long title is cut to DUMP_NAME_MAX, never inside a character. */
    char t[400] = "";
    for (int i = 0; i < 60; i++) strcat(t, "ä"); /* 2 bytes each */
    strcat(t, " and more words after it to make it really long indeed");
    char out[256];
    dump_file_name(t, "ULES00003", out, sizeof(out));
    int l = (int)strlen(out);
    if (l > DUMP_NAME_MAX || strcmp(out + l - 16, " [ULES00003].iso")) {
        printf("FAIL long name: \"%s\" (%d bytes)\n", out, l);
        fails++;
    }
    for (int i = 0; i < l - 16; i += 2)
        if ((unsigned char)out[i] != 0xC3 || (unsigned char)out[i + 1] != 0xA4) {
            printf("FAIL long name cut inside a character at %d: \"%s\"\n", i, out);
            fails++;
            break;
        }
    /* A small buffer still gives a whole name with the ID. */
    char small[24];
    dump_file_name("A rather long game title", "ULES00004", small, sizeof(small));
    if (strcmp(small, "A rath [ULES00004].iso") && strcmp(small, "A rathe [ULES00004].iso")) {
        printf("FAIL small buffer: \"%s\"\n", small);
        fails++;
    }

    /* PVD */
    unsigned char s[2048];
    memset(s, 0, sizeof(s));
    s[0] = 1;
    memcpy(s + 1, "CD001", 5);
    unsigned v = 734208; /* a 1.4 GB disc */
    s[80] = v; s[81] = v >> 8; s[82] = v >> 16; s[83] = v >> 24;
    s[84] = v >> 24; s[85] = v >> 16; s[86] = v >> 8; s[87] = v;
    if (dump_pvd_sectors(s) != v) { printf("FAIL pvd: %u\n", dump_pvd_sectors(s)); fails++; }
    s[87] ^= 1;
    if (dump_pvd_sectors(s) != 0) { printf("FAIL pvd mismatch accepted\n"); fails++; }
    s[87] ^= 1;
    s[1] = 'X';
    if (dump_pvd_sectors(s) != 0) { printf("FAIL pvd without CD001 accepted\n"); fails++; }

    /* The disc's size */
    struct { int end; unsigned pvd; int reads; unsigned want; } ds[] = {
        {247023, 247024, 0, 247024}, /* a real drive (PSP-E1004, Burnout Legends) */
        {247024, 247024, 0, 247024}, /* ARK's emulated drive */
        {247023, 0, 1, 247024},      /* no descriptor: the last sector reads */
        {247023, 0, 0, 247023},      /* ... or it doesn't */
        {247023, 100000, 1, 247024}, /* a descriptor that doesn't fit: the drive wins */
        {-1, 247024, 0, 247024},     /* no end: the descriptor */
        {-1, 0, 0, 0},
        {5, 0, 0, 0},
    };
    for (unsigned i = 0; i < sizeof(ds) / sizeof(ds[0]); i++) {
        unsigned got = dump_disc_sectors(ds[i].end, ds[i].pvd, ds[i].reads);
        if (got != ds[i].want) { printf("FAIL disc_sectors(%d, %u, %d) = %u, want %u\n", ds[i].end, ds[i].pvd, ds[i].reads, got, ds[i].want); fails++; }
    }

    /* Time left */
    const uint64_t MB = 1 << 20;
    left(10 * MB, 100 * MB, 4000, "");                    /* too early */
    left(100 * 1024, 100 * MB, 10000, "");                /* too little read */
    left(100 * MB, 100 * MB, 60000, "");                  /* done */
    left(50 * MB, 100 * MB, 20000, "less than a minute left");
    left(100 * MB, 1500 * MB, 50000, "about 12 min left"); /* 2 MB/s, 1400 MB left = 700 s */
    left(10 * MB, 1810 * MB, 20000, "about 1 h left");     /* 0.5 MB/s, 3600 s */
    left(10 * MB, 1960 * MB, 20000, "about 1 h 5 min left");

    if (fails) {
        printf("test_dumpname: %d FAILED\n", fails);
        return 1;
    }
    printf("test_dumpname: all passed\n");
    return 0;
}
