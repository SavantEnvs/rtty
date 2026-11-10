/* Shared per-iteration state handling for the harnesses that include ../src/command.c. */
#ifndef MAYHEM_HARNESS_COMMON_H
#define MAYHEM_HARNESS_COMMON_H

#include "log/log.h"

static void harness_init_rtty(struct rtty *rtty, struct ev_loop *loop)
{
    memset(rtty, 0, sizeof(*rtty));
    INIT_LIST_HEAD(&rtty->ttys);
    INIT_LIST_HEAD(&rtty->http_conns);
    INIT_LIST_HEAD(&rtty->tcp_conns);
    rtty->loop = loop;
    rtty->sock = -1;
    rtty->shell = "/bin/sh";
    rtty->devid = "fuzz";
    rtty->heartbeat = 30;
    rtty->http_timeout = 30;
}

/* Free every task run_command() started or queued, so each input starts from the same state. */
static void harness_free_tasks(void)
{
    struct task *t, *n;
    int i;

    for (i = 0; i < mayhem_ntask_children; i++)
        task_free(container_of(mayhem_task_children[i], struct task, cw));
    mayhem_ntask_children = 0;
    nrunning = 0;

    list_for_each_entry_safe(t, n, &task_pending, list) {
        list_del(&t->list);
        task_free(t);
    }
}

static void harness_free_rtty_io(struct rtty *rtty)
{
    ev_io_stop(rtty->loop, &rtty->iow);
    ev_io_stop(rtty->loop, &rtty->ior);
    ev_timer_stop(rtty->loop, &rtty->tmr);
    buffer_free(&rtty->rb);
    buffer_free(&rtty->wb);
}

#endif
