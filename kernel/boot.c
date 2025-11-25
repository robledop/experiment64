#include <limine.h>
#include <stddef.h>
#include "terminal.h"
#include "cpu.h"

__attribute__((used, section(".requests_start"))) static volatile LIMINE_REQUESTS_START_MARKER
    __attribute__((used, section(".requests_end"))) static volatile LIMINE_REQUESTS_END_MARKER

    __attribute__((used, section(".requests"))) static volatile LIMINE_BASE_REVISION(2)

        __attribute__((used, section(".requests"))) volatile struct limine_framebuffer_request framebuffer_request = {
            .id = LIMINE_FRAMEBUFFER_REQUEST,
            .revision = 0};

__attribute__((used, section(".requests"))) volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0};

__attribute__((used, section(".requests"))) volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0};

void boot_init(void)
{
    if (LIMINE_BASE_REVISION_SUPPORTED == false)
    {
        hcf();
    }

    if (hhdm_request.response == nullptr)
    {
        hcf();
    }
}

void boot_init_terminal(void)
{
    if (framebuffer_request.response == nullptr || framebuffer_request.response->framebuffer_count < 1)
    {
        hcf();
    }

    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];
    terminal_init(framebuffer);
}

uint64_t boot_get_hhdm_offset(void)
{
    if (hhdm_request.response)
        return hhdm_request.response->offset;
    return 0;
}

struct limine_smp_response *boot_get_smp_response(void)
{
    return smp_request.response;
}
