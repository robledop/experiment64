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


static inline uint8_t read8(const uint64_t addr)
{
    return *(volatile uint8_t *)(uintptr_t)addr;
}

static inline uint16_t read16(const uint64_t addr)
{
    return *(volatile uint16_t *)(uintptr_t)addr;
}

static inline uint32_t read32(const uint64_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline uint64_t read64(const uint64_t addr)
{
    return *(volatile uint64_t *)(uintptr_t)addr;
}

static inline void write8(const uint64_t addr, const uint8_t data)
{
    *(volatile uint8_t *)(uintptr_t)addr = data;
}

static inline void write16(const uint64_t addr, const uint16_t data)
{
    *(volatile uint16_t *)(uintptr_t)addr = data;
}

static inline void write32(const uint64_t addr, const uint32_t data)
{
    *(volatile uint32_t *)(uintptr_t)addr = data;
}

static inline void write64(const uint64_t addr, const uint64_t data)
{
    *(volatile uint64_t *)(uintptr_t)addr = data;
}

static inline void stack_push_pointer(char **stack_pointer, const uint32_t value)
{
    *(uint32_t *)stack_pointer -= sizeof(uint32_t); // make room for a pointer
    **(uint32_t **)stack_pointer = value;      // push the pointer onto the stack
}

static inline uint32_t inl(uint16_t p)
{
    uint32_t r;
    __asm__ volatile("in %0, %1" : "=a"(r) : "d"(p));
    return r;
}

static inline void insl(int port, void *addr, int cnt)
{
    __asm__ volatile("cld; rep insl" :
        "=D" (addr), "=c" (cnt) :
        "d" (port), "0" (addr), "1" (cnt) :
        "memory", "cc");
}

static inline void outsl(int port, const void *addr, int cnt)
{
    __asm__ volatile("cld; rep outsl" :
        "=S" (addr), "=c" (cnt) :
        "d" (port), "0" (addr), "1" (cnt) :
        "cc");
}

static inline void outl(uint16_t portid, uint32_t value)
{
    __asm__ volatile("out %1, %0" : : "a"(value), "d"(portid));
}

static inline void hlt(void)
{
    __asm__ volatile("hlt");
}
