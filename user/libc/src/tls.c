#include <tls.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

extern char __tdata_start[] __attribute__((weak));
extern char __tdata_end[] __attribute__((weak));
extern char __tbss_start[] __attribute__((weak));
extern char __tbss_end[] __attribute__((weak));

static size_t span_bytes(const void *start, const void *end)
{
    uintptr_t start_addr = (uintptr_t)start;
    uintptr_t end_addr   = (uintptr_t)end;
    if (end_addr < start_addr)
        return 0;
    return (size_t)(end_addr - start_addr);
}

static inline void wrfsbase(uint64_t val)
{
    __asm__ volatile ("wrfsbase %0" :: "r"(val));
}

static size_t tls_tdata_size(void)
{
    if (!__tdata_start || !__tdata_end)
        return 0;
    return span_bytes(__tdata_start, __tdata_end);
}

static size_t tls_total_size(void)
{
    if (!__tdata_start)
        return 0;
    if (__tbss_end)
        return span_bytes(__tdata_start, __tbss_end);
    if (__tdata_end)
        return span_bytes(__tdata_start, __tdata_end);
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
