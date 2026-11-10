/*
 * Drives the remote-command request parser and task queue (run_command in src/command.c) with the payload
 * of a MSG_TYPE_CMD frame: username\0 cmd\0 token\0 nparams params... The process creation calls are the
 * stand-ins from mock.h/mock.c, so the real argument parsing, user lookup, command lookup, queueing and
 * error replies run, and nothing is executed.
 */
#include <stdint.h>
#include <stddef.h>

#include "../src/command.c"
#include "harness_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    log_level(-1);
    mayhem_task_child_cb = ev_child_exit;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static struct rtty rtty;
    struct ev_loop *loop = ev_default_loop(0);

    harness_init_rtty(&rtty, loop);

    run_command(&rtty, (const char *)data, (int)size);

    harness_free_tasks();
    harness_free_rtty_io(&rtty);
    return 0;
}
