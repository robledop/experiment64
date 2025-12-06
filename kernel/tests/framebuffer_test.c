#include "test.h"
#include "vfs.h"
#include "ioctl.h"
#include "framebuffer.h"
#include "mman.h"
#include "syscall.h"
#include "vmm.h"
#include "process.h"
#include "fcntl.h"
#include <stdint.h>
#include "string.h"

// Kernel syscall entry points (not exposed in headers)
extern int sys_open(const char *path, int flags);
extern int sys_close(int fd);
extern void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset);
extern int sys_munmap(void *addr, size_t length);

TEST(test_fb_device_exists)
{
    vfs_inode_t *fb = vfs_resolve_path("/dev/fb0");
    TEST_ASSERT(fb != nullptr);
    TEST_ASSERT((fb->flags & VFS_CHARDEVICE) != 0);
    return true;
}

TEST(test_fb_ioctl_basic)
{
    vfs_inode_t *fb = vfs_resolve_path("/dev/fb0");
    TEST_ASSERT(fb != nullptr);

    struct limine_framebuffer *fbhw = framebuffer_current();
    TEST_ASSERT(fbhw != nullptr);

    uint32_t width = 0, height = 0, pitch = 0;
    uint64_t fbaddr = 0;

    TEST_ASSERT(vfs_ioctl(fb, FB_IOCTL_GET_WIDTH, &width) == 0);
    TEST_ASSERT(vfs_ioctl(fb, FB_IOCTL_GET_HEIGHT, &height) == 0);
    TEST_ASSERT(vfs_ioctl(fb, FB_IOCTL_GET_PITCH, &pitch) == 0);
    TEST_ASSERT(vfs_ioctl(fb, FB_IOCTL_GET_FBADDR, &fbaddr) == 0);

    TEST_ASSERT(width == fbhw->width);
    TEST_ASSERT(height == fbhw->height);
    TEST_ASSERT(pitch == fbhw->pitch);
    TEST_ASSERT(fbaddr == (uint64_t)fbhw->address);
    return true;
}

TEST(test_fb_mmap_and_munmap)
{
    int fd = sys_open("/dev/fb0", O_RDWR);
    TEST_ASSERT(fd >= 0);

    struct limine_framebuffer *fb = framebuffer_current();
    TEST_ASSERT(fb != nullptr);
    uint64_t fb_addr = (uint64_t)fb->address;
    uint64_t fb_phys = (fb_addr >= g_hhdm_offset) ? (fb_addr - g_hhdm_offset) : fb_addr;
    TEST_ASSERT(fb_phys != 0);

    size_t len = PAGE_SIZE;
    void *map = sys_mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    TEST_ASSERT(map != MAP_FAILED);

    uint64_t map_base = (uint64_t)map & ~(PAGE_SIZE - 1);
    uint64_t phys = vmm_virt_to_phys(kernel_process->pml4, map_base);
    TEST_ASSERT(phys == fb_phys);

    // Touch the mapping lightly
    volatile uint8_t *p = (uint8_t *)map;
    uint8_t orig = *p;
    *p = (uint8_t)(orig ^ 0xFF);
    *p = orig;

    TEST_ASSERT(sys_munmap((void *)map_base, PAGE_SIZE) == 0);
    TEST_ASSERT(sys_close(fd) == 0);
    return true;
}
