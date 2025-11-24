#pragma once

#include <stdint.h>

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("in %0, %1" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("out %1, %0" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile("in %0, %1" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("out %1, %0" : : "a"(val), "Nd"(port));
}

static inline uint32_t ind(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile("in %0, %1" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outd(uint16_t port, uint32_t val)
{
    __asm__ volatile("out %1, %0" : : "a"(val), "Nd"(port));
}

static inline void insw(uint16_t port, void *addr, uint32_t cnt)
{
    __asm__ volatile("rep insw" : "+D"(addr), "+c"(cnt) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, const void *addr, uint32_t cnt)
{
    __asm__ volatile("rep outsw" : "+S"(addr), "+c"(cnt) : "d"(port) : "memory");
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}
