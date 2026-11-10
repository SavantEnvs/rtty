/*
 * Drives the file transfer state machine (parse_file_msg in src/file.c) with the FILE messages a server
 * sends to the daemon. Input: one selector byte for the transfer already in progress (the local client has
 * connected and asked to download a file, or to upload into a directory), then a series of messages, each
 * a 2-byte big-endian length and that many message bytes (type + body). Uploads land in a scratch
 * directory under $TMPDIR (default /tmp) that is emptied after every input.
 */
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>

#include "log/log.h"
#include "../src/term.h"

#define SOURCE_NAME "source.bin"
#define SOURCE_SIZE 600

static char scratch[4096];

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    const char *tmp = getenv("TMPDIR");

    log_level(-1);
    snprintf(scratch, sizeof(scratch), "%s/rtty-fuzz-file-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(scratch))
        return 1;
    return 0;
}

static void empty_scratch(int dirfd)
{
    int fd = dup(dirfd);
    DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
    struct dirent *e;

    if (!d) {
        if (fd >= 0)
            close(fd);
        return;
    }

    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            unlinkat(dirfd, e->d_name, 0);
    }
    closedir(d);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static struct tty_term term;
    static struct rtty rtty;
    struct file_context *ctx = &term.file;
    struct ev_loop *loop = ev_default_loop(0);
    int sv[2];
    int dirfd;
    size_t off = 1;

    if (size < 1)
        return 0;

    dirfd = open(scratch, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd < 0)
        return 0;

    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) < 0) {
        close(dirfd);
        return 0;
    }

    memset(&rtty, 0, sizeof(rtty));
    INIT_LIST_HEAD(&rtty.ttys);
    rtty.loop = loop;
    rtty.sock = -1;

    memset(&term, 0, sizeof(term));
    term.tty.rtty = &rtty;
    memset(term.tty.sid, 'a', 32);
    term.pty = -1;

    memset(ctx, 0, sizeof(*ctx));
    ctx->listenfd = -1;
    ctx->fd = -1;
    ctx->dirfd = -1;
    ctx->ctlfd = sv[0];
    ctx->uid = getuid();
    ctx->gid = getgid();
    ctx->create_mode = 0644;

    if (data[0] & 1) {
        char content[SOURCE_SIZE];
        int fd;

        memset(content, 'x', sizeof(content));
        fd = openat(dirfd, SOURCE_NAME, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd >= 0 && write(fd, content, sizeof(content)) == (ssize_t)sizeof(content)) {
            lseek(fd, 0, SEEK_SET);
            ctx->fd = fd;
            ctx->dirfd = dup(dirfd);
            strcpy(ctx->name, SOURCE_NAME);
            ctx->total = ctx->remaining = SOURCE_SIZE;
            ctx->state = FILE_SEND_ACK;
        } else if (fd >= 0) {
            close(fd);
        }
        if (ctx->state == FILE_IDLE) {
            close(sv[0]);
            close(sv[1]);
            close(dirfd);
            return 0;
        }
    } else {
        ctx->dirfd = dup(dirfd);
        ctx->state = FILE_RECV_INFO;
    }

    while (off + 2 <= size) {
        size_t len = ((size_t)data[off] << 8) | data[off + 1];

        off += 2;
        if (len > size - off)
            len = size - off;

        parse_file_msg(ctx, data + off, len);
        off += len;
    }

    empty_scratch(dirfd);
    file_context_close(ctx);

    close(dirfd);
    close(sv[1]);
    buffer_free(&rtty.wb);
    ev_io_stop(loop, &rtty.iow);
    return 0;
}
