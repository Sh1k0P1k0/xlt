/* updparse.c: GitHub's release JSON, version order, plain notes, the EBOOT in the zip. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "updparse.h"

/* The shape of api.github.com/repos/.../releases/latest (trimmed), nested objects and nulls included. */
static const char REAL[] =
    "{\"url\":\"https://api.github.com/repos/o/xlt/releases/1\",\"id\":261,"
    "\"author\":{\"login\":\"someone\",\"id\":1,\"site_admin\":false,\"user_view_type\":\"public\"},"
    "\"tag_name\":\"v1.2.0\",\"target_commitish\":\"main\",\"name\":\"XLT 1.2.0\",\"draft\":false,"
    "\"immutable\":false,\"prerelease\":false,\"created_at\":\"2026-11-12T20:00:00Z\","
    "\"assets\":[{\"url\":\"https://api.github.com/x\",\"id\":5,\"name\":\"notes.txt\",\"label\":null,"
    "\"uploader\":{\"login\":\"someone\",\"id\":1},\"content_type\":\"text/plain\",\"state\":\"uploaded\","
    "\"size\":12,\"digest\":null,\"download_count\":3,"
    "\"browser_download_url\":\"https://github.com/o/xlt/releases/download/v1.2.0/notes.txt\"},"
    "{\"url\":\"https://api.github.com/y\",\"id\":6,\"name\":\"XLT-1.2.0.zip\",\"label\":\"\","
    "\"uploader\":{\"login\":\"someone\",\"id\":1},\"content_type\":\"application/zip\",\"state\":\"uploaded\","
    "\"size\":2075288,\"digest\":\"sha256:9F42263C711748ECDD1705788192004D72F203A388E7D99FCE1C375E040B8B11\","
    "\"download_count\":21,\"created_at\":\"2026-11-12T20:17:00Z\","
    "\"browser_download_url\":\"https://github.com/o/xlt/releases/download/v1.2.0/XLT-1.2.0.zip\"}],"
    "\"tarball_url\":\"https://api.github.com/t\",\"zipball_url\":\"https://api.github.com/z\","
    "\"body\":\"The **second** release.\\r\\n\\r\\n## What's new\\r\\n\\r\\n- **Update check:** in [About](https://x/y).\\r\\n"
    "* `code` and caf\\u00e9 \\ud83d\\ude00\\r\\n\\r\\n\\r\\n\\r\\nLast \\\"line\\\"\\r\\n\"}";

int main(void)
{
    UpdRelease r;
    assert(upd_parse_release(REAL, (int)strlen(REAL), &r) == 0);
    assert(!strcmp(r.tag, "v1.2.0"));
    assert(!strcmp(r.version, "1.2.0"));
    assert(!r.draft && !r.prerelease);
    assert(!strcmp(r.zip_name, "XLT-1.2.0.zip"));
    assert(!strcmp(r.zip_url, "https://github.com/o/xlt/releases/download/v1.2.0/XLT-1.2.0.zip"));
    assert(r.zip_size == 2075288);
    assert(!strcmp(r.sha256, "9f42263c711748ecdd1705788192004d72f203a388e7d99fce1c375e040b8b11"));
    assert(strstr(r.notes, "caf\xC3\xA9 \xF0\x9F\x98\x80")); /* é and a surrogate pair */
    assert(strstr(r.notes, "Last \"line\""));

    char plain[1024];
    upd_plain_notes(r.notes, plain, sizeof(plain));
    assert(!strcmp(plain, "The second release.\n\nWhat's new\n\n- Update check: in About.\n- code and caf\xC3\xA9 "
                          "\xF0\x9F\x98\x80\n\nLast \"line\""));
    /* cut short: never in the middle of a UTF-8 character */
    char tiny[30];
    upd_plain_notes("abcdefghijklmnopqrstuvwxyz\xC3\xA9\xC3\xA9", tiny, sizeof(tiny));
    assert(!strcmp(tiny, "abcdefghijklmnopqrstuvwxyz\xC3\xA9"));

    /* every truncation of the JSON fails cleanly (ASan watches for overreads) */
    int n = (int)strlen(REAL);
    for (int k = 0; k < n; k++) {
        char *cut = malloc(k ? k : 1);
        memcpy(cut, REAL, k);
        assert(upd_parse_release(cut, k, &r) == -1);
        free(cut);
    }

    /* not a release */
    const char *bad[] = {
        "", "[]", "{}", "{\"message\":\"Not Found\",\"documentation_url\":\"https://docs.github.com\"}",
        "{\"tag_name\":\"v1.0.0\",\"assets\":[]}",                       /* no zip */
        "{\"tag_name\":\"latest\",\"assets\":[{\"name\":\"a.zip\",\"browser_download_url\":\"https://x/a.zip\"}]}",
        "{\"tag_name\":\"v1\",\"assets\":[{\"name\":\"a.zip\",\"browser_download_url\":\"http://x/a.zip\"}]}",
        "{\"tag_name\":\"v1\" \"assets\":[]}", "{\"tag_name\":\"v1\\x\"}",
    };
    for (int i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++)
        assert(upd_parse_release(bad[i], (int)strlen(bad[i]), &r) == -1);
    /* any zip is taken, an XLT one is preferred; no digest leaves sha256 empty */
    const char *two = "{\"tag_name\":\"1.3\",\"prerelease\":true,\"assets\":["
                      "{\"name\":\"src.zip\",\"browser_download_url\":\"https://x/src.zip\",\"size\":1},"
                      "{\"name\":\"xlt-1.3.ZIP\",\"browser_download_url\":\"https://x/xlt.zip\",\"size\":2,\"digest\":\"md5:00\"},"
                      "{\"name\":\"XLT-other.zip\",\"browser_download_url\":\"https://x/other.zip\"}]}";
    assert(upd_parse_release(two, (int)strlen(two), &r) == 0);
    assert(r.prerelease && !strcmp(r.version, "1.3"));
    assert(!strcmp(r.zip_url, "https://x/xlt.zip") && r.zip_size == 2 && !r.sha256[0]);
    const char *nul = "{\"tag_name\":\"v2.0.0\",\"body\":null,\"draft\":true,\"assets\":[{\"name\":\"XLT.zip\","
                      "\"browser_download_url\":\"https://x/XLT.zip\"}]}";
    assert(upd_parse_release(nul, (int)strlen(nul), &r) == 0);
    assert(r.draft && !r.notes[0] && r.zip_size == -1);

    /* versions */
    assert(upd_version_cmp("1.0.0", "1.0.0") == 0);
    assert(upd_version_cmp("v1.0.1", "1.0.0") > 0);
    assert(upd_version_cmp("1.0.0", "V1.0.1") < 0);
    assert(upd_version_cmp("1.10.0", "1.9.9") > 0);
    assert(upd_version_cmp("1.1", "1.1.0") == 0);
    assert(upd_version_cmp("2", "1.99.99") > 0);
    assert(upd_version_cmp("1.1.0-beta", "1.1.0") < 0);
    assert(upd_version_cmp("1.1.0", "1.1.0-beta") > 0);
    assert(upd_version_cmp("1.1.0-beta", "1.0.9") > 0);
    assert(upd_version_cmp("1.1.0-rc2", "1.1.0-rc1") > 0);

    /* which file is XLT */
    assert(upd_eboot_rank("PSP/GAME/XLT/EBOOT.PBP") == 2);
    assert(upd_eboot_rank("/psp/game/xlt/eboot.pbp") == 2);
    assert(upd_eboot_rank("XLT/EBOOT.PBP") == 1);
    assert(upd_eboot_rank("EBOOT.PBP") == 1);
    assert(upd_eboot_rank("PSP/GAME/XLT/README.txt") == 0);
    assert(upd_eboot_rank("PSP/GAME/XLT/MY_EBOOT.PBP") == 0);
    assert(upd_eboot_rank("PSP/GAME/XLT/") == 0);

    printf("test_updparse: ok\n");
    return 0;
}
