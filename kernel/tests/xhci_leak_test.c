#include <tests/test.h>
#include <drivers/usb/xhci_internal.h>
#include <mem/pmm.h>
#include <lib/string.h>

/**
 * xhci_free_device_context must reclaim every page allocated by
 * xhci_alloc_device_context + xhci_prepare_slot_context. This is the cleanup
 * the enumeration-failure path relies on; a forgotten free leaks DMA pages.
 */
TEST(test_xhci_free_device_context_no_leak)
{
    if (!g_xhci.dcbaa || g_xhci.max_slots < 3)
        return true; // No xHCI controller in this configuration — nothing to test.

    struct xhci_device scratch;
    memset(&scratch, 0, sizeof(scratch));
    scratch.slot_id = (uint8_t)g_xhci.max_slots; // highest valid slot, unused by real devices
    scratch.port_id = 1;
    scratch.speed   = 3; // high speed (must be non-zero for prepare_slot_context)

    const uint64_t saved_dcbaa = g_xhci.dcbaa[scratch.slot_id];
    const size_t before        = pmm_count_free_pages();

    TEST_ASSERT(xhci_alloc_device_context(&g_xhci, &scratch));
    TEST_ASSERT(xhci_prepare_slot_context(&g_xhci, &scratch));
    xhci_free_device_context(&g_xhci, &scratch);

    const size_t after = pmm_count_free_pages();
    g_xhci.dcbaa[scratch.slot_id] = saved_dcbaa; // restore the (unused) DCBAA slot

    TEST_ASSERT(scratch.device_ctx == nullptr);
    TEST_ASSERT(scratch.input_ctx == nullptr);
    TEST_ASSERT(scratch.ep0_ring.trbs == nullptr);
    TEST_ASSERT(after == before); // every allocated page reclaimed
    return true;
}
