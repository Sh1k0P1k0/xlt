/* XLT - the home menu's card order (see homeorder.h). */
#include <string.h>
#include <strings.h>
#include "homeorder.h"

#define MAX_CARDS 32

void homeorder_parse(const char *text, const char *const *keys, int n, int *order)
{
    char used[MAX_CARDS] = {0};
    int k = 0;
    if (n > MAX_CARDS) n = MAX_CARDS;
    for (const char *p = text; p && *p;) {
        while (*p == ' ' || *p == ',') p++;
        const char *end = p;
        while (*end && *end != ',') end++;
        size_t len = (size_t)(end - p);
        while (len && p[len - 1] == ' ') len--; /* "games , music" */
        for (int c = 0; c < n && len; c++)
            if (!used[c] && strlen(keys[c]) == len && !strncasecmp(p, keys[c], len)) {
                used[c] = 1;
                order[k++] = c;
                break;
            }
        p = end;
    }
    for (int c = 0; c < n; c++) /* the cards it didn't name, in the default order */
        if (!used[c])
            order[k++] = c;
}

int homeorder_format(const int *order, const char *const *keys, int n, char *out, int cap)
{
    int len = 0;
    if (cap <= 0)
        return -1;
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        const char *key = keys[order[i]];
        int need = (int)strlen(key) + (i > 0);
        if (len + need >= cap) {
            out[0] = 0;
            return -1;
        }
        if (i > 0) out[len++] = ',';
        memcpy(out + len, key, strlen(key));
        len += (int)strlen(key);
        out[len] = 0;
    }
    return len;
}

int homeorder_move(int *order, int n, int pos, int dir)
{
    int to = pos + (dir < 0 ? -1 : 1);
    if (pos < 0 || pos >= n || to < 0 || to >= n)
        return -1;
    int t = order[pos];
    order[pos] = order[to];
    order[to] = t;
    return to;
}
