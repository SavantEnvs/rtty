/*
 * Drives rtty's network message dispatcher (parse_msg in src/rtty.c) with a raw byte stream, exactly as
 * on_net_read() does after reading from the server socket: frames are type(1) + length(2, BE) + payload.
 * Reaches register/heartbeat, terminal sessions (open/data/winsize/logout/ack), file messages, HTTP
 * proxying, TCP forwarding, serial port listing and remote command execution. The environment calls
 * (fork/forkpty/connect/getpwnam/scandir) are the stand-ins from mock.h/mock.c.
 */
#include <stdint.h>
#include <stddef.h>
#include <sys/mman.h>

#include "../src/rtty.c"
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
    rtty.sock = memfd_create("sock", MFD_CLOEXEC);
    if (rtty.sock < 0)
        return 0;

    if (size)
        buffer_put_data(&rtty.rb, data, size);

    if (parse_msg(&rtty))
        rtty_exit(&rtty);

    if (rtty.sock >= 0)
        rtty_exit(&rtty);

    harness_free_tasks();
    harness_free_rtty_io(&rtty);
    return 0;
}
