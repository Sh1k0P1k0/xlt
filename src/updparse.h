/* XLT - the update check's pure parts: GitHub's "latest release" JSON, version
 * numbers, release notes as plain text, and which file in the release zip is
 * XLT. Portable (host-tested: tests/test_updparse.c); update.c does the rest. */
#ifndef UPDPARSE_H
#define UPDPARSE_H

#include <stdint.h>

#define UPD_NOTES_MAX 3072

typedef struct {
    char tag[32];             /* "v1.1.0" */
    char version[32];         /* "1.1.0" (the tag without its v) */
    int draft, prerelease;
    char notes[UPD_NOTES_MAX]; /* the release text, as GitHub has it (Markdown) */
    char zip_name[96];
    char zip_url[512];
    int64_t zip_size;         /* -1 if not given */
    char sha256[65];          /* lower-case hex, "" if GitHub gave no digest */
} UpdRelease;

/* Reads GitHub's /releases/latest answer. 0, or -1 if it isn't one (no tag, or
 * no .zip among the assets). Unknown keys are skipped. */
int upd_parse_release(const char *json, int len, UpdRelease *r);

/* <0, 0, >0 like strcmp, by number: "1.10.0" > "1.9.2", a leading v is ignored,
 * missing parts are 0 ("1.1" == "1.1.0"), and "1.1.0-beta" < "1.1.0". */
int upd_version_cmp(const char *a, const char *b);

/* Markdown to plain lines: no #, **, `, link targets; "- " and "* " items keep a
 * "- "; CR dropped; blank runs become one blank line. */
void upd_plain_notes(const char *md, char *out, int n);

/* How good a name inside the release zip is as XLT's EBOOT.PBP: 2 for
 * PSP/GAME/XLT/EBOOT.PBP, 1 for any other .../EBOOT.PBP or a bare EBOOT.PBP, 0 for
 * the rest. Case is ignored. */
int upd_eboot_rank(const char *name);

#endif
