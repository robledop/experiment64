#include <netdb.h>
#include <paths.h>
#include <libgen.h>
#include <fnmatch.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/statfs.h>
#include <pwd.h>
#include <grp.h>
#include <mntent.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <glob.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    (void)node;
    (void)service;
    (void)hints;
    (void)res;
    errno = EUNIMP;
    return -1;
}

void freeaddrinfo(struct addrinfo *res)
{
    (void)res;
}

struct hostent *gethostbyname(const char *name)
{
    (void)name;
    return nullptr;
}

char **environ = nullptr;

static int g_h_errno;

int *__h_errno_location(void)
{
    return &g_h_errno;
}

const char *hstrerror(int err)
{
    (void)err;
    return "Unknown host error";
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    errno = EUNIMP;
    return -1;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    errno = EUNIMP;
    return -1;
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    errno = EUNIMP;
    return -1;
}

struct servent *getservbyname(const char *name, const char *proto)
{
    (void)name;
    (void)proto;
    return nullptr;
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host,
                socklen_t hostlen, char *serv, socklen_t servlen, int flags)
{
    (void)sa;
    (void)salen;
    (void)host;
    (void)hostlen;
    (void)serv;
    (void)servlen;
    (void)flags;
    errno = EUNIMP;
    return -1;
}

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
    (void)tv;
    (void)tz;
    errno = EUNIMP;
    return -1;
}

clock_t times(struct tms *buf)
{
    if (buf) {
        buf->tms_utime = 0;
        buf->tms_stime = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return (clock_t)-1;
}

int glob(const char *pattern, int flags, int (*errfunc)(const char *epath, int eerrno), glob_t *pglob)
{
    (void)pattern;
    (void)flags;
    (void)errfunc;
    if (pglob) {
        pglob->gl_pathc = 0;
        pglob->gl_pathv = nullptr;
        pglob->gl_offs = 0;
    }
    return GLOB_NOMATCH;
}

void globfree(glob_t *pglob)
{
    (void)pglob;
}

int getrlimit(int resource, struct rlimit *rlim)
{
    (void)resource;
    if (rlim) {
        rlim->rlim_cur = RLIM_INFINITY;
        rlim->rlim_max = RLIM_INFINITY;
    }
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim)
{
    (void)resource;
    (void)rlim;
    errno = EUNIMP;
    return -1;
}

int uname(struct utsname *buf)
{
    if (!buf)
        return -1;
    strncpy(buf->sysname, "experiment64", _UTSNAME_LENGTH - 1);
    buf->sysname[_UTSNAME_LENGTH - 1] = '\0';
    strncpy(buf->nodename, "localhost", _UTSNAME_LENGTH - 1);
    buf->nodename[_UTSNAME_LENGTH - 1] = '\0';
    strncpy(buf->release, "1", _UTSNAME_LENGTH - 1);
    buf->release[_UTSNAME_LENGTH - 1] = '\0';
    strncpy(buf->version, "", _UTSNAME_LENGTH - 1);
    strncpy(buf->machine, "x86_64", _UTSNAME_LENGTH - 1);
    buf->machine[_UTSNAME_LENGTH - 1] = '\0';
    return 0;
}

static char dirname_buf[2] = ".";
static char basename_buf[2] = ".";

char *dirname(char *path)
{
    (void)path;
    return dirname_buf;
}

char *basename(char *path)
{
    (void)path;
    return basename_buf;
}

int getrusage(int who, struct rusage *usage)
{
    (void)who;
    (void)usage;
    errno = EUNIMP;
    return -1;
}

struct passwd *getpwuid(int uid)
{
    (void)uid;
    return nullptr;
}

struct passwd *getpwnam(const char *name)
{
    (void)name;
    return nullptr;
}

void endpwent(void)
{
}

struct group *getgrgid(int gid)
{
    (void)gid;
    return nullptr;
}

struct group *getgrnam(const char *name)
{
    (void)name;
    return nullptr;
}

void endgrent(void)
{
}

int initgroups(const char *user, gid_t gid)
{
    (void)user;
    (void)gid;
    errno = ENOSYS;
    return -1;
}

FILE *setmntent(const char *filename, const char *type)
{
    (void)filename;
    (void)type;
    return nullptr;
}

struct mntent *getmntent(FILE *stream)
{
    (void)stream;
    return nullptr;
}

int endmntent(FILE *stream)
{
    (void)stream;
    return 1;
}

int statfs(const char *path, struct statfs *buf)
{
    (void)path;
    (void)buf;
    errno = EUNIMP;
    return -1;
}

int fnmatch(const char *pattern, const char *string, int flags)
{
    (void)pattern;
    (void)string;
    (void)flags;
    return FNM_NOMATCH;
}

int mallopt(int param, int value)
{
    (void)param;
    (void)value;
    return 0;
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    errno = EUNIMP;
    return -1;
}
