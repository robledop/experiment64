#include "acpi.h"
#include "limine.h"
#include <stddef.h>
#include "string.h"
#include <limits.h>

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
        size_t entries = (xsdt->length - sizeof(struct sdt_header)) / 8;
        if (entries > (size_t)INT_MAX)
            entries = INT_MAX;
        uint8_t *tables_ptr = (uint8_t *)xsdt + sizeof(struct sdt_header);

        for (int i = 0; i < (int)entries; i++)
        {
            uint64_t table_addr;
            memcpy(&table_addr, tables_ptr + i * 8, 8);
            struct sdt_header *table = (struct sdt_header *)(table_addr + hhdm_offset);
            if (strncmp(table->signature, signature, 4) == 0)
            {
                return table;
            }
        }
    }
    else if (rsdt != NULL)
    {
        size_t entries = (rsdt->length - sizeof(struct sdt_header)) / 4;
        if (entries > (size_t)INT_MAX)
            entries = INT_MAX;
        uint8_t *tables_ptr = (uint8_t *)rsdt + sizeof(struct sdt_header);

        for (int i = 0; i < (int)entries; i++)
        {
            uint32_t table_addr;
            memcpy(&table_addr, tables_ptr + i * 4, 4);
            struct sdt_header *table = (struct sdt_header *)((uint64_t)table_addr + hhdm_offset);
            if (strncmp(table->signature, signature, 4) == 0)
            {
                return table;
            }
        }
    }

    return NULL;
}
