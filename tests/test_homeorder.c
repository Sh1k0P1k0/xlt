/* The home menu's card order (src/homeorder.c): a saved order is read back as
 * it was, unknown and repeated names are skipped, cards it doesn't name come
 * last, and moving stops at the ends. */
#include <stdio.h>
#include <string.h>
#include "../src/homeorder.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); puts(""); fails++; } } while (0)

static const char *const KEYS[] = {"homebrew", "games", "music", "video", "pictures", "files", "web", "settings"};
#define N 8

/* order as text, to compare in one go */
static const char *str(const int *order, int n)
{
    static char buf[128];
    int len = homeorder_format(order, KEYS, n, buf, sizeof(buf));
    return len < 0 ? "(too long)" : buf;
}

static void parse_is(const char *text, const char *want)
{
    int order[N];
    memset(order, -1, sizeof(order));
    homeorder_parse(text, KEYS, N, order);
    CHECK(!strcmp(str(order, N), want), "parse \"%s\" -> %s, want %s", text ? text : "(null)", str(order, N), want);
}

int main(void)
{
    const char *def = "homebrew,games,music,video,pictures,files,web,settings";
    parse_is(NULL, def);
    parse_is("", def);
    parse_is(def, def);
    parse_is("settings,web,files,pictures,video,music,games,homebrew", "settings,web,files,pictures,video,music,games,homebrew");
    /* only some named (a card added later): the rest follow in the default order */
    parse_is("music,games", "music,games,homebrew,video,pictures,files,web,settings");
    /* a card that went away, a typo, the UMD (never in the saved order) */
    parse_is("umd,games,radio,Music,gmaes", "games,music,homebrew,video,pictures,files,web,settings");
    /* repeats, spaces, empty items, case */
    parse_is(" games , games,,MUSIC ,", "games,music,homebrew,video,pictures,files,web,settings");
    /* a prefix of a key isn't the key */
    parse_is("game,set,web", "web,homebrew,games,music,video,pictures,files,settings");
    parse_is(",,,", def);

    /* round trip after moves */
    int o[N];
    homeorder_parse(NULL, KEYS, N, o);
    CHECK(homeorder_move(o, N, 0, -1) == -1, "first card can't go left");
    CHECK(homeorder_move(o, N, N - 1, 1) == -1, "last card can't go right");
    CHECK(homeorder_move(o, N, -1, 1) == -1 && homeorder_move(o, N, N, -1) == -1, "out of range");
    CHECK(!strcmp(str(o, N), def), "failed moves left it alone");
    int at = 1; /* games, carried to the far right */
    for (int i = 0; i < N - 2; i++)
        at = homeorder_move(o, N, at, 1);
    CHECK(at == N - 1, "games at the end (%d)", at);
    CHECK(homeorder_move(o, N, at, 1) == -1, "and no further");
    CHECK(!strcmp(str(o, N), "homebrew,music,video,pictures,files,web,settings,games"), "after moves: %s", str(o, N));
    at = homeorder_move(o, N, 6, -1); /* settings one left */
    CHECK(at == 5, "settings at 5 (%d)", at);
    char text[128];
    homeorder_format(o, KEYS, N, text, sizeof(text));
    int back[N];
    homeorder_parse(text, KEYS, N, back);
    CHECK(!memcmp(o, back, sizeof(o)), "round trip of %s", text);

    /* format: too small a buffer gives -1 and "" */
    char small[10];
    CHECK(homeorder_format(o, KEYS, N, small, sizeof(small)) == -1 && small[0] == 0, "too small");
    CHECK(homeorder_format(o, KEYS, N, text, (int)strlen(def) + 1) == (int)strlen(def), "exact fit");
    CHECK(homeorder_format(o, KEYS, N, text, (int)strlen(def)) == -1, "one short");

    if (fails) { printf("test_homeorder: %d failure(s)\n", fails); return 1; }
    puts("test_homeorder: ok");
    return 0;
}
