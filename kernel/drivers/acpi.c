#include "acpi.h"
#include "limine.h"
#include <stddef.h>
#include "string.h"

extern volatile struct limine_hhdm_request hhdm_request;

__attribute__((used, section(".requests"))) static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0};

void *acpi_find_table(const char *signature)
{
    if (rsdp_request.response == NULL || rsdp_request.response->address == NULL || hhdm_request.response == NULL)
    {
        return NULL;
    }

    uint64_t hhdm_offset = hhdm_request.response->offset;
    struct rsdp *rsdp = (struct rsdp *)rsdp_request.response->address;
    struct sdt_header *xsdt = NULL;
    struct sdt_header *rsdt = NULL;

    if (rsdp->revision >= 2 && ((struct xsdp *)rsdp)->xsdt_address != 0)
    {
        xsdt = (struct sdt_header *)(((struct xsdp *)rsdp)->xsdt_address + hhdm_offset);
    }
    else
    {
        rsdt = (struct sdt_header *)((uint64_t)rsdp->rsdt_address + hhdm_offset);
    }

    if (xsdt != NULL)
    {
        int entries = (xsdt->length - sizeof(struct sdt_header)) / 8;
        uint64_t *tables = (uint64_t *)((uint8_t *)xsdt + sizeof(struct sdt_header));

        for (int i = 0; i < entries; i++)
        {
            struct sdt_header *table = (struct sdt_header *)(tables[i] + hhdm_offset);
            if (strncmp(table->signature, signature, 4) == 0)
            {
                return table;
            }
        }
    }
    else if (rsdt != NULL)
    {
        int entries = (rsdt->length - sizeof(struct sdt_header)) / 4;
        uint32_t *tables = (uint32_t *)((uint8_t *)rsdt + sizeof(struct sdt_header));

        for (int i = 0; i < entries; i++)
        {
            struct sdt_header *table = (struct sdt_header *)((uint64_t)tables[i] + hhdm_offset);
            if (strncmp(table->signature, signature, 4) == 0)
            {
                return table;
            }
        }
    }

    return NULL;
}
