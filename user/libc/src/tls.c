#include <tls.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

extern char __tdata_start[] __attribute__((weak));
extern char __tdata_end[] __attribute__((weak));
extern char __tbss_start[] __attribute__((weak));
extern char __tbss_end[] __attribute__((weak));

static inline void wrfsbase(uint64_t val)
{
    __asm__ volatile ("wrfsbase %0" :: "r"(val));
}

static size_t tls_tdata_size(void)
{
    if (!__tdata_start || !__tdata_end)
        return 0;
    return (size_t)(__tdata_end - __tdata_start);
}

static size_t tls_total_size(void)
{
    if (!__tdata_start)
        return 0;
    if (__tbss_end)
        return (size_t)(__tbss_end - __tdata_start);
    if (__tdata_end)
        return (size_t)(__tdata_end - __tdata_start);
    return 0;
}

static void tls_setup(void)
{
    size_t tdata_sz = tls_tdata_size();
    size_t total_sz = tls_total_size();
    size_t aligned  = (total_sz + 63) & ~(size_t)63;
    size_t alloc_sz = aligned + sizeof(struct tcb);

    char *block = calloc(1, alloc_sz);
    if (!block)
        return;

    struct tcb *tcb       = (struct tcb *)(block + aligned);
    char *tls_data_start  = (char *)tcb - total_sz;

    if (tdata_sz > 0)
        memcpy(tls_data_start, __tdata_start, tdata_sz);

    tcb->self = tcb;
    wrfsbase((uint64_t)tcb);
}

void __tls_init(void)
{
    tls_setup();
}

void __tls_init_thread(void)
{
    tls_setup();
}

void __tls_destroy_thread(void)
{
    uint64_t fs;
    __asm__ volatile ("rdfsbase %0" : "=r"(fs));
    if (fs == 0)
        return;

    struct tcb *tcb = (struct tcb *)fs;
    size_t total_sz = tls_total_size();
    size_t aligned  = (total_sz + 63) & ~(size_t)63;
    char *block     = (char *)tcb - aligned;
    free(block);

    wrfsbase(0);
}
