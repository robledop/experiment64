#pragma once

#include <stdint.h>
#include <sys/socket.h>

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    unsigned ai_addrlen;
    char *ai_canonname;
    void *ai_addr;
    struct addrinfo *ai_next;
};

#define AI_PASSIVE 1
#define AI_NUMERICHOST 2
#define AI_CANONNAME 4

#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2
#define NI_NUMERICSCOPE 4
#define NI_NAMEREQD 8

struct servent {
    char  *s_name;
    char **s_aliases;
    int    s_port;
    char  *s_proto;
};

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);

struct hostent {
    char  *h_name;
    char **h_aliases;
    int    h_addrtype;
    int    h_length;
    char **h_addr_list;
};
#define h_addr h_addr_list[0]

struct hostent *gethostbyname(const char *name);

int *__h_errno_location(void);
#define h_errno (*__h_errno_location())
const char *hstrerror(int err);

struct servent *getservbyname(const char *name, const char *proto);
int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host,
                socklen_t hostlen, char *serv, socklen_t servlen, int flags);
