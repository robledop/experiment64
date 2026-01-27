#include <drivers/usb/xhci.h>
#include <drivers/usb/ehci.h>


void xhci_init(struct pci_device device)
{
    // Only xHCI is supported
    if (device.prog_if != 0x30) {
        return;
    }

    ehci_quiesce_all();
}

