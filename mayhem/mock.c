#include <errno.h>
#include <sys/mman.h>
#include <netinet/in.h>

#undef ev_child_start

#define FAKE_PID 424242

void (*mayhem_task_child_cb)(struct ev_loop *, ev_child *, int);
ev_child *mayhem_task_children[MAYHEM_MAX_TASKS];
int mayhem_ntask_children;

int mayhem_forkpty(int *amaster, char *name, const struct termios *termp, const struct winsize *winp)
{
    int fd = memfd_create("pty", MFD_CLOEXEC);

    if (fd < 0)
        return -1;
    *amaster = fd;
    return FAKE_PID + 1;
}

pid_t mayhem_fork(void)
{
    return FAKE_PID;
}

int mayhem_kill(pid_t pid, int sig)
{
    return 0;
}

int mayhem_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    if (addr->sa_family == AF_INET && len >= sizeof(struct sockaddr_in) &&
        (((const struct sockaddr_in *)addr)->sin_port & 0x100)) {
        errno = ECONNREFUSED;
        return -1;
    }

    errno = EINPROGRESS;
    return -1;
}

struct passwd *mayhem_getpwnam(const char *name)
{
    static struct passwd pw;

    if (strcmp(name, "root") && strcmp(name, "mayhem"))
        return NULL;

    pw.pw_name = (char *)name;
    pw.pw_uid = name[0] == 'r' ? 0 : 2000;
    pw.pw_gid = pw.pw_uid;
    return &pw;
}

int mayhem_scandir(const char *dir, struct dirent ***namelist,
                   int (*filter)(const struct dirent *),
                   int (*compar)(const struct dirent **, const struct dirent **))
{
    *namelist = malloc(sizeof(**namelist));
    return *namelist ? 0 : -1;
}

uid_t mayhem_geteuid(void)
{
    return 1000;
}

void mayhem_ev_child_start(struct ev_loop *loop, ev_child *w)
{
    if (mayhem_task_child_cb && w->cb == mayhem_task_child_cb && mayhem_ntask_children < MAYHEM_MAX_TASKS)
        mayhem_task_children[mayhem_ntask_children++] = w;

    ev_child_start(loop, w);
}
