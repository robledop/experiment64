# ACPI (`acpi.c`, `acpi.h`)

This kernel only uses ACPI to locate static tables (primarily the MADT for
APIC/IOAPIC setup). There is no AML/DSDT parsing or power-management support.

---

## Data Structures

The ACPI table headers and pointer structures live in `include/acpi.h`:

- `rsdp` / `xsdp`: Root System Description Pointer (ACPI 1.0 vs 2.0+).
- `sdt_header`: common header for all ACPI tables.
- `madt` and MADT entry structs (used by the APIC code).

---

## `acpi_find_table(signature)`

`acpi_find_table(const char *signature)` returns a pointer to the first ACPI
table that matches a 4-byte signature (for example, `"APIC"` for the MADT).

Lookup flow:

1. Fetch the RSDP from the Limine RSDP request.
2. If `rsdp->revision >= 2` and `xsdt_address` is non-zero, use the XSDT.
3. Otherwise fall back to the 32-bit RSDT.
4. Walk table pointers and compare `sdt_header::signature` against the input.
5. Return the mapped table header, or `nullptr` if not found.

Addresses inside the XSDT/RSDT are physical, so the code adds the HHDM offset
when dereferencing them.

Example usage (from APIC init):

```c
struct madt *madt = acpi_find_table("APIC");
```