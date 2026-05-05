#include "shared.h"

int main(void)
{
    // 1. Archive type detection
    {
        struct { const char *url; Nob_Fetch_Archive_Type expected; } cases[] = {
            {"https://x.com/a.tar.gz", NOB_FETCH_ARCHIVE_TAR_GZ},
            {"https://x.com/a.tgz",    NOB_FETCH_ARCHIVE_TAR_GZ},
            {"https://x.com/a.tar.xz", NOB_FETCH_ARCHIVE_TAR_XZ},
            {"https://x.com/a.tar.bz2",NOB_FETCH_ARCHIVE_TAR_BZ2},
            {"https://x.com/a.zip",    NOB_FETCH_ARCHIVE_ZIP},
            {"https://x.com/a.h?raw=true", NOB_FETCH_ARCHIVE_NONE},
            {"https://x.com/a.h#L10",      NOB_FETCH_ARCHIVE_NONE},
            {"https://x.com/a",            NOB_FETCH_ARCHIVE_NONE},
        };
        for (size_t i = 0; i < ARRAY_LEN(cases); i++) {
            if (nob__fetch_detect_archive_type(cases[i].url) != cases[i].expected) return 1;
        }
    }
    printf("archive_type: OK\n");

    // 2. URL cleaning
    {
        struct { const char *input; const char *expected; } cases[] = {
            {"https://x.com/a.h?raw=true", "https://x.com/a.h"},
            {"https://x.com/a.tar.gz#anchor", "https://x.com/a.tar.gz"},
            {"https://x.com/a", "https://x.com/a"},
            {"https://x.com/f?x=1#y", "https://x.com/f"},
        };
        for (size_t i = 0; i < ARRAY_LEN(cases); i++) {
            size_t mark = temp_save();
            const char *got = nob__fetch_clean_url(cases[i].input);
            if (strcmp(got, cases[i].expected) != 0) return 1;
            temp_rewind(mark);
        }
    }
    printf("clean_url: OK\n");

    // 3. Extract tar.gz with strip_components=1
    {
        mkdir_if_not_exists("srcdir");
        write_entire_file("srcdir/data.txt", "hello", 5);
        Cmd cmd = {0};
        cmd_append(&cmd, "tar", "-czf", "test.tar.gz", "srcdir");
        if (!cmd_run(&cmd)) return 1;

        mkdir_if_not_exists("out1");
        if (!nob__fetch_extract("test.tar.gz", "out1", NOB_FETCH_ARCHIVE_TAR_GZ, 1)) return 1;
        if (!file_exists("out1/data.txt")) return 1;
    }
    printf("extract_tar_gz_strip1: OK\n");

    // 4. Extract tar.gz with strip_components=0
    {
        mkdir_if_not_exists("out0");
        if (!nob__fetch_extract("test.tar.gz", "out0", NOB_FETCH_ARCHIVE_TAR_GZ, 0)) return 1;
        if (!file_exists("out0/srcdir/data.txt")) return 1;
    }
    printf("extract_tar_gz_strip0: OK\n");

    // 5. Full flow: single file download via file:// URL
    //    Set up the file, "download" it, verify the include directory structure
    {
        write_entire_file("remote_header.h", "// header", 9);
        // Simulate what nob_fetch_content would do for a single file:
        // download the file into _deps/<name>/<filename>
        mkdir_if_not_exists("_deps");
        mkdir_if_not_exists("_deps/hdr");
        // Use nob_download_file with a file:// URL
        const char *cwd = nob_get_current_dir_temp();
        String_Builder url_sb = {0};
#ifdef _WIN32
        sb_appendf(&url_sb, "file:///%s/remote_header.h", cwd);
#else
        sb_appendf(&url_sb, "file://%s/remote_header.h", cwd);
#endif
        for (size_t i = 0; i < url_sb.count; i++) {
            if (url_sb.items[i] == '\\') url_sb.items[i] = '/';
        }
        sb_append_null(&url_sb);

        // nob_download_file handles the actual curl call
        if (!nob_download_file(url_sb.items, "_deps/hdr/remote_header.h")) return 1;
        if (!file_exists("_deps/hdr/remote_header.h")) return 1;
        // Write stamp so cache hit works
        write_entire_file("_deps/hdr/.nob-fetch-stamp", url_sb.items, strlen(url_sb.items));
        sb_free(url_sb);
    }
    printf("download_single_file: OK\n");

    // 6. Full flow: archive download + extraction
    {
        const char *cwd = nob_get_current_dir_temp();
        String_Builder url_sb = {0};
#ifdef _WIN32
        sb_appendf(&url_sb, "file:///%s/test.tar.gz", cwd);
#else
        sb_appendf(&url_sb, "file://%s/test.tar.gz", cwd);
#endif
        for (size_t i = 0; i < url_sb.count; i++) {
            if (url_sb.items[i] == '\\') url_sb.items[i] = '/';
        }
        sb_append_null(&url_sb);

        const char *dir = NULL;
        if (!nob_fetch_content_opt(
            (Nob_Fetch_Content_Opt){.url = url_sb.items, .name = "lib"}, &dir)) return 1;
        if (!file_exists("_deps/lib/data.txt")) return 1;
        sb_free(url_sb);
    }
    printf("download_archive: OK\n");

    // 7. Cache hit: re-fetch skips download
    {
        const char *cwd = nob_get_current_dir_temp();
        String_Builder url_sb = {0};
#ifdef _WIN32
        sb_appendf(&url_sb, "file:///%s/test.tar.gz", cwd);
#else
        sb_appendf(&url_sb, "file://%s/test.tar.gz", cwd);
#endif
        for (size_t i = 0; i < url_sb.count; i++) {
            if (url_sb.items[i] == '\\') url_sb.items[i] = '/';
        }
        sb_append_null(&url_sb);
        const char *dir = NULL;
        if (!nob_fetch_content_opt(
            (Nob_Fetch_Content_Opt){.url = url_sb.items, .name = "lib"}, &dir)) return 1;
        if (!file_exists("_deps/lib/data.txt")) return 1;
        sb_free(url_sb);
    }
    printf("cache_hit: OK\n");

    // 8. deps_dir override
    {
        mkdir_if_not_exists("custom");
        mkdir_if_not_exists("custom/mydep");
        write_entire_file("custom/mydep/.nob-fetch-stamp", "fake_url", 8);
        const char *dir = NULL;
        if (!nob_fetch_content_opt(
            (Nob_Fetch_Content_Opt){.url = "fake_url", .name = "mydep", .deps_dir = "custom"}, &dir)) return 1;
        if (strcmp(dir, "custom/mydep") != 0) return 1;
    }
    printf("deps_dir_override: OK\n");

    // 9. include_dir=NULL is safe
    {
        if (!nob_fetch_content_opt(
            (Nob_Fetch_Content_Opt){.url = "fake_url", .name = "lib"}, NULL)) return 1;
    }
    printf("null_include_dir: OK\n");

    // 10. Name defaults to URL filename
    {
        mkdir_if_not_exists("_deps/header.h");
        write_entire_file("_deps/header.h/.nob-fetch-stamp", "fake", 4);
        const char *dir = NULL;
        if (!nob_fetch_content_opt(
            (Nob_Fetch_Content_Opt){.url = "https://x.com/header.h"}, &dir)) return 1;
        if (strcmp(dir, "_deps/header.h") != 0) return 1;
    }
    printf("name_from_url: OK\n");

    // 11. .git URL → archive URL conversion
    {
        struct { const char *url; const char *tag; const char *expected; } cases[] = {
            {"https://github.com/raysan5/raylib.git", "5.0",
                "https://github.com/raysan5/raylib/archive/5.0.tar.gz"},
            {"https://github.com/owner/repo.git", NULL,
                "https://github.com/owner/repo/archive/HEAD.tar.gz"},
            {"https://github.com/owner/repo.git", "main",
                "https://github.com/owner/repo/archive/main.tar.gz"},
            {"https://github.com/owner/repo.git", "abc123",
                "https://github.com/owner/repo/archive/abc123.tar.gz"},
        };
        for (size_t i = 0; i < ARRAY_LEN(cases); i++) {
            size_t mark = temp_save();
            const char *got = nob__fetch_git_to_archive(cases[i].url, cases[i].tag);
            if (!got) return 1;
            if (strcmp(got, cases[i].expected) != 0) return 1;
            temp_rewind(mark);
        }
        // Non-.git URL returns NULL
        if (nob__fetch_git_to_archive("https://x.com/file.tar.gz", NULL) != NULL) return 1;
    }
    printf("git_to_archive: OK\n");

    // 11b. "latest" tag resolution (requires network, smoke test only)
    {
        size_t mark = temp_save();
        const char *got = nob__fetch_git_to_archive("https://github.com/tsoding/nob.h.git", "latest");
        if (!got) return 1;
        (void)got;
        temp_rewind(mark);
    }
    printf("git_latest_smoke: OK\n");

    // 12. .git URL name resolution: name = repo without .git
    {
        // Create stamp so it's a cache hit (stamp includes URL + tag)
        mkdir_if_not_exists("_deps/raylib");
        const char *stamp = "https://github.com/raysan5/raylib.git 5.0";
        write_entire_file("_deps/raylib/.nob-fetch-stamp", stamp, strlen(stamp));
        const char *dir = NULL;
        if (!nob_fetch_content_opt(
            (Nob_Fetch_Content_Opt){.url = "https://github.com/raysan5/raylib.git", .tag = "5.0"},
            &dir)) return 1;
        if (strcmp(dir, "_deps/raylib") != 0) return 1;
    }
    printf("git_url_name: OK\n");

    // 13. .git URL with explicit .name override
    {
        mkdir_if_not_exists("_deps/mylib");
        write_entire_file("_deps/mylib/.nob-fetch-stamp",
            "https://github.com/x/y.git", strlen("https://github.com/x/y.git"));
        const char *dir = NULL;
        if (!nob_fetch_content_opt(
            (Nob_Fetch_Content_Opt){
                .url = "https://github.com/x/y.git",
                .name = "mylib",
            }, &dir)) return 1;
        if (strcmp(dir, "_deps/mylib") != 0) return 1;
    }
    printf("git_url_name_override: OK\n");

    printf("ALL TESTS PASSED\n");
    return 0;
}
