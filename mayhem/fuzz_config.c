/*
 * Drives rtty's INI configuration loader (load_config_file / apply_cfg_pair / apply_config_file in
 * src/main.c) with the contents of a config file, then applies the parsed settings to a daemon context.
 */
#include <stdint.h>
#include <stddef.h>

#define main rtty_main
#include "../src/main.c"
#undef main

static char path[4096];
static int fd = -1;

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    const char *tmp = getenv("TMPDIR");

    log_level(-1);
    snprintf(path, sizeof(path), "%s/rtty-fuzz-config-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    fd = mkstemp(path);
    return fd < 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct config cfg;
    struct rtty rtty;

    if (ftruncate(fd, 0) < 0 || pwrite(fd, data, size, 0) != (ssize_t)size)
        return 0;

    memset(&cfg, 0, sizeof(cfg));
    memset(&rtty, 0, sizeof(rtty));

    if (!load_config_file(path, &cfg))
        apply_config_file(&rtty, &cfg);

    free_config(&cfg);
    return 0;
}
