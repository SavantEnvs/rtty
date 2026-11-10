/*
 * Force-included (-include) into every rtty source compiled for fuzzing and for the selftest.
 * rtty is a daemon that forks shells, runs commands as other users, opens outbound TCP connections
 * and enumerates /dev. This renames those environment calls (at the preprocessor, so ASan's
 * interceptors keep working and no real target code is replaced) to the deterministic stand-ins in
 * mock.c. Everything else (parsing, buffering, state machines, the libev watchers) is the real code.
 */
#ifndef MAYHEM_MOCK_H
#define MAYHEM_MOCK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <math.h>
#include <pwd.h>
#include <dirent.h>
#include <termios.h>
#include <pty.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ev.h>

int mayhem_forkpty(int *amaster, char *name, const struct termios *termp, const struct winsize *winp);
pid_t mayhem_fork(void);
int mayhem_kill(pid_t pid, int sig);
int mayhem_connect(int fd, const struct sockaddr *addr, socklen_t len);
struct passwd *mayhem_getpwnam(const char *name);
int mayhem_scandir(const char *dir, struct dirent ***namelist,
                   int (*filter)(const struct dirent *),
                   int (*compar)(const struct dirent **, const struct dirent **));
uid_t mayhem_geteuid(void);
void mayhem_ev_child_start(struct ev_loop *loop, ev_child *w);

/* Child watchers whose callback is mayhem_task_child_cb are remembered so a harness can free them. */
#define MAYHEM_MAX_TASKS 64
extern void (*mayhem_task_child_cb)(struct ev_loop *, ev_child *, int);
extern ev_child *mayhem_task_children[MAYHEM_MAX_TASKS];
extern int mayhem_ntask_children;

#define forkpty mayhem_forkpty
#define fork mayhem_fork
#define kill mayhem_kill
#define connect mayhem_connect
#define getpwnam mayhem_getpwnam
#define scandir mayhem_scandir
#define geteuid mayhem_geteuid
#define ev_child_start mayhem_ev_child_start

#endif
