/* Config-file checks of the selftest; separate translation unit because src/main.c is included whole. */
#include <stdint.h>

#define main rtty_main
#include "../src/main.c"
#undef main

static int pass, fail;

#define CHECK(name, cond) do { \
        if (cond) { pass++; printf("PASS config %s\n", name); } \
        else { fail++; printf("FAIL config %s (line %d)\n", name, __LINE__); } \
    } while (0)

static int write_file(char *path, const char *text)
{
    int fd = mkstemp(path);

    if (fd < 0)
        return -1;
    if (write(fd, text, strlen(text)) != (ssize_t)strlen(text)) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int selftest_config(void)
{
    char good[] = "/tmp/rtty-selftest-XXXXXX";
    char badport[] = "/tmp/rtty-selftest-XXXXXX";
    char spaced[] = "/tmp/rtty-selftest-XXXXXX";
    struct config cfg;
    struct rtty rtty;

    if (write_file(good, "[rtty]\nid=dev1\nhost=example.org\nport=5912\nheartbeat=1000\n"
                   "description=lab router\ntoken=abc123\nreconnect=yes\nhttp-timeout=2\n") ||
        write_file(badport, "[rtty]\nport=notanumber\n") ||
        write_file(spaced, "[rtty]\nid=has space\n")) {
        printf("FAIL config tempfiles\n");
        return -1;
    }

    memset(&cfg, 0, sizeof(cfg));
    memset(&rtty, 0, sizeof(rtty));
    CHECK("load good file", load_config_file(good, &cfg) == 0);
    CHECK("parsed strings", cfg.devid && !strcmp(cfg.devid, "dev1") && !strcmp(cfg.host, "example.org") &&
          !strcmp(cfg.description, "lab router") && !strcmp(cfg.token, "abc123"));
    CHECK("parsed numbers", cfg.has_port && cfg.port == 5912 && cfg.has_heartbeat && cfg.heartbeat == 1000 &&
          cfg.has_reconnect && cfg.reconnect);
    CHECK("apply good file", apply_config_file(&rtty, &cfg) == 0);
    CHECK("applied to context", !strcmp(rtty.devid, "dev1") && !strcmp(rtty.host, "example.org") &&
          rtty.port == 5912 && rtty.reconnect && !strcmp(rtty.token, "abc123"));
    CHECK("heartbeat clamped to 255", rtty.heartbeat == 255);
    CHECK("http timeout clamped to 5", rtty.http_timeout == 5);
    free_config(&cfg);

    memset(&cfg, 0, sizeof(cfg));
    CHECK("bad port rejected", load_config_file(badport, &cfg) == -1);
    free_config(&cfg);

    memset(&cfg, 0, sizeof(cfg));
    memset(&rtty, 0, sizeof(rtty));
    CHECK("id with space loads", load_config_file(spaced, &cfg) == 0);
    CHECK("id with space not applied", apply_config_file(&rtty, &cfg) == -1);
    free_config(&cfg);

    memset(&cfg, 0, sizeof(cfg));
    CHECK("missing file rejected", load_config_file("/nonexistent/rtty.ini", &cfg) == -1);
    free_config(&cfg);

    unlink(good);
    unlink(badport);
    unlink(spaced);

    printf("CONFIG pass=%d fail=%d\n", pass, fail);
    return fail;
}
