/* XLT - the Reader's text editor: the parts that don't draw or touch files.
 * Portable (host-tested: tests/test_textedit.c); reader.c does the rest.
 *
 * The Reader shows a file as UTF-8 without its BOM and carriage returns
 * (textenc.c). A file can be edited only when that is all it loses, so
 * that saving gives back the same bytes for every line not changed:
 * valid UTF-8, no control characters but tab and line breaks, a CR only
 * before an LF, not UTF-16, and at most TED_MAX bytes. */
#ifndef TEXTEDIT_H
#define TEXTEDIT_H

#include <stdint.h>

#define TED_MAX (256 << 10)

typedef struct {
    int bom;  /* it starts with the UTF-8 BOM */
    int crlf; /* its lines end in CR LF (most of them, if mixed) */
} TedFormat;

/* 1 if the file's n bytes can be edited (f filled), else 0 and *why says why. */
int ted_check(const uint8_t *s, int n, TedFormat *f, const char **why);

/* The text (as the Reader holds it) as file bytes again: the BOM and the CRs
 * put back. Returns a malloc'd buffer and its length, or NULL. */
uint8_t *ted_encode(const char *t, int len, const TedFormat *f, int *out_len);

/* The line around byte off: its first byte, and the byte after its last one
 * (its '\n', or len). */
int ted_line_start(const char *t, int len, int off);
int ted_line_end(const char *t, int len, int off);
/* The line number (1-based) of byte off, and how many lines the text has
 * (a final '\n' starts one more, empty, line, as editors show it). */
int ted_line_no(const char *t, int len, int off);
int ted_line_count(const char *t, int len);

/* t with del bytes at off replaced by ins: a new malloc'd, NUL-terminated
 * buffer and its length, or NULL. t is not changed. */
char *ted_splice(const char *t, int len, int off, int del, const char *ins, int ins_len, int *out_len);

#endif
