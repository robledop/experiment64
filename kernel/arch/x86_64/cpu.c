#include "string.h"
#include "cpu.h"
#include "terminal.h"

static bool g_use_xsave = false;
static bool g_use_xsaveopt = false;
static bool g_avx_enabled = false;
static uint64_t g_xsave_mask = XCR0_X87 | XCR0_SSE;
static uint32_t g_fpu_save_size = 512;

// NOLINTBEGIN(readability-non-const-parameter)
static inline void cpuid(uint32_t leaf, uint32_t subleaf,
                         uint32_t *const eax, uint32_t *const ebx,
                         uint32_t *const ecx, uint32_t *const edx)
{
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

// NOLINTEND(readability-non-const-parameter)

static inline void xsetbv(uint32_t index, uint64_t value)
{
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("xsetbv" : : "c"(index), "a"(low), "d"(high) : "memory");
}

void enable_simd(void)
{
    uint64_t cr0;
    uint64_t cr4;

    __asm__ volatile("mov %0, cr0" : "=r"(cr0));
    cr0 &= ~(1 << 2); // Clear EM (Emulation)
    cr0 |= (1 << 1);  // Set MP (Monitor Co-processor)
    cr0 &= ~(1 << 3); // Clear TS (Task Switched)
    __asm__ volatile("mov cr0, %0" ::"r"(cr0));

    __asm__ volatile("mov %0, cr4" : "=r"(cr4));
    cr4 |= (1 << 9);  // Set OSFXSR (OS Support for FXSAVE/FXRSTOR)
    cr4 |= (1 << 10); // Set OSXMMEXCPT (OS Support for Unmasked SIMD Floating-Point Exceptions)

    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);

    const bool has_xsave = (ecx & (1u << 26)) != 0;
    const bool has_avx = (ecx & (1u << 28)) != 0;

    g_use_xsave = has_xsave;
    g_avx_enabled = false;
    g_use_xsaveopt = false;
    g_xsave_mask = XCR0_X87 | XCR0_SSE;

    if (g_use_xsave)
    {
        cr4 |= (1 << 18); // Set OSXSAVE (enable XSETBV/XGETBV)
    }

    __asm__ volatile("mov cr4, %0" ::"r"(cr4));

    if (g_use_xsave)
    {
        cpuid(0xD, 0, &eax, &ebx, &ecx, &edx);
        uint64_t supported = ((uint64_t)edx << 32) | eax;
        g_xsave_mask &= supported;
        if (has_avx && (supported & XCR0_AVX))
            g_xsave_mask |= XCR0_AVX;

        // Enable states in XCR0 (x87 + SSE + AVX if present)
        xsetbv(0, g_xsave_mask);

        // Size of XSAVE area for currently enabled features
        cpuid(0xD, 0, &eax, &ebx, &ecx, &edx);
        g_fpu_save_size = (ebx > FPU_STATE_SIZE || ebx == 0) ? FPU_STATE_SIZE : ebx;

        // XSAVEOPT support
        cpuid(0xD, 1, &eax, &ebx, &ecx, &edx);
        g_use_xsaveopt = (eax & 1u) != 0;

        g_avx_enabled = (g_xsave_mask & XCR0_AVX) != 0;
    }
    else
    {
        g_fpu_save_size = 512;
    }

    __asm__ volatile("fninit");
    uint32_t mxcsr = 0x1F80; // Mask all exceptions
    __asm__ volatile("ldmxcsr %0" ::"m"(mxcsr));
}

void save_fpu_state(fpu_state_t *state)
{
    if (g_use_xsave)
    {
        uint32_t low = (uint32_t)g_xsave_mask;
        uint32_t high = (uint32_t)(g_xsave_mask >> 32);
        if (g_use_xsaveopt) // NOLINT(bugprone-branch-clone) different instruction variant
            __asm__ volatile("xsaveopt %0" : "=m"(state->data) : "a"(low), "d"(high) : "memory");
        else
            __asm__ volatile("xsave %0" : "=m"(state->data) : "a"(low), "d"(high) : "memory");
    }
    else
    {
        __asm__ volatile("fxsave %0" : "=m"(state->data) : : "memory");
    }
}

void restore_fpu_state(fpu_state_t *state)
{
    if (g_use_xsave)
    {
        uint32_t low = (uint32_t)g_xsave_mask;
        uint32_t high = (uint32_t)(g_xsave_mask >> 32);
        __asm__ volatile("xrstor %0" : : "m"(state->data), "a"(low), "d"(high) : "memory");
    }
    else
    {
        __asm__ volatile("fxrstor %0" : : "m"(state->data));
    }
}

bool cpu_is_hypervisor(void)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    return (ecx & (1u << 31)) != 0;
}

void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

cpu_t *get_cpu(void)
{
    cpu_t *cpu;
    __asm__ volatile("mov %0, gs:[0]" : "=r"(cpu));
    return cpu;
}

void hcf(void)
{
    __asm__ volatile("cli");
    for (;;)
    {
        __asm__("hlt");
    }
}

void init_fpu_state(fpu_state_t *state)
{
    uint32_t size = g_fpu_save_size;
    if (size == 0 || size > FPU_STATE_SIZE)
        size = FPU_STATE_SIZE;
    memset(state->data, 0, size);

    // Set MXCSR to default (0x1F80) - All exceptions masked
    // MXCSR is at offset 24
    uint32_t *mxcsr = (uint32_t *)&state->data[24];
    *mxcsr = 0x1F80;

    // Set FCW to default (0x037F) - Extended precision, all exceptions masked
    uint16_t *fcw = (uint16_t *)&state->data[0];
    *fcw = 0x037F;

    if (g_use_xsave)
    {
        // XSAVE header starts at offset 512
        uint64_t *xstate_bv = (uint64_t *)&state->data[512];
        uint64_t *xcomp_bv = (uint64_t *)&state->data[520];
        *xstate_bv = g_xsave_mask;
        *xcomp_bv = 0;
    }
}

bool cpu_has_avx(void)
{
    return g_avx_enabled;
}

uint32_t cpu_fpu_save_size(void)
{
    return g_fpu_save_size;
}

// MTRR Memory Type definitions
#define MTRR_TYPE_UC 0 // Uncacheable
#define MTRR_TYPE_WC 1 // Write Combining
#define MTRR_TYPE_WT 4 // Write Through
#define MTRR_TYPE_WP 5 // Write Protect
#define MTRR_TYPE_WB 6 // Write Back

#define IA32_MTRRCAP 0xFE
#define IA32_MTRR_DEF_TYPE 0x2FF
#define IA32_MTRR_PHYSBASE0 0x200
#define IA32_MTRR_PHYSMASK0 0x201

const char *mtrr_type_name(uint8_t type)
{
    switch (type)
    {
    case MTRR_TYPE_UC:
        return "UC (Uncacheable)";
    case MTRR_TYPE_WC:
        return "WC (Write Combining)";
    case MTRR_TYPE_WT:
        return "WT (Write Through)";
    case MTRR_TYPE_WP:
        return "WP (Write Protect)";
    case MTRR_TYPE_WB:
        return "WB (Write Back)";
    default:
        return "Unknown";
    }
}

void cpu_dump_mtrr_for_address(uint64_t phys_addr)
{
    uint64_t mtrrcap = rdmsr(IA32_MTRRCAP);
    uint8_t vcnt = mtrrcap & 0xFF; // Number of variable MTRRs

    uint64_t def_type = rdmsr(IA32_MTRR_DEF_TYPE);
    uint8_t default_type = def_type & 0xFF;
    bool mtrr_enabled = (def_type & (1 << 11)) != 0;

    boot_message(INFO, "MTRR: enabled=%d, default_type=%s, variable_count=%d",
                 mtrr_enabled, mtrr_type_name(default_type), vcnt);
    boot_message(INFO, "Checking address 0x%lx:", phys_addr);

    uint8_t effective_type = default_type;
    bool found_match = false;

    for (uint8_t i = 0; i < vcnt && i < 10; i++)
    {
        uint64_t base = rdmsr(IA32_MTRR_PHYSBASE0 + i * 2);
        uint64_t mask = rdmsr(IA32_MTRR_PHYSMASK0 + i * 2);

        bool valid = (mask & (1 << 11)) != 0;
        if (!valid)
            continue;

        uint64_t base_addr = base & 0xFFFFFFFFF000ULL;
        uint64_t mask_addr = mask & 0xFFFFFFFFF000ULL;
        uint8_t type = base & 0xFF;

        // Check if address matches this MTRR
        if ((phys_addr & mask_addr) == (base_addr & mask_addr))
        {
            boot_message(INFO, "  MTRR[%d]: base=0x%lx mask=0x%lx type=%s << MATCH",
                         i, base_addr, mask_addr, mtrr_type_name(type));
            effective_type = type;
            found_match = true;
        }
        else
        {
            boot_message(INFO, "  MTRR[%d]: base=0x%lx mask=0x%lx type=%s",
                         i, base_addr, mask_addr, mtrr_type_name(type));
        }
    }

    if (!found_match)
        boot_message(INFO, "  No variable MTRR match, using default type");

    boot_message(INFO, "Effective memory type for 0x%lx: %s", phys_addr, mtrr_type_name(effective_type));
}

// Set up an MTRR to mark a physical range as Write-Combining
// NOTE: MTRR modification is disabled because:
// 1. Hypervisors don't allow it (GP fault)
// 2. Real hardware often has MTRRs locked by firmware (GP fault)
// 3. All MTRR slots are typically used by firmware for RAM
// The framebuffer with UC memory type is the reality we have to work with.
bool cpu_set_mtrr_wc(uint64_t phys_base, uint64_t size)
{
    (void)phys_base;
    (void)size;

    if (cpu_is_hypervisor())
    {
        boot_message(INFO, "MTRR: Skipping (hypervisor)");
        return false;
    }

    // Check MTRR capabilities for diagnostics
    uint64_t mtrrcap = rdmsr(IA32_MTRRCAP);
    uint8_t vcnt = mtrrcap & 0xFF;
    bool has_wc = (mtrrcap & (1 << 10)) != 0;

    uint64_t def_type = rdmsr(IA32_MTRR_DEF_TYPE);
    uint8_t default_type = def_type & 0xFF;

    boot_message(INFO, "MTRR: %d slots, WC=%s, default=%s - not modifying (firmware lock risk)",
                 vcnt, has_wc ? "yes" : "no", mtrr_type_name(default_type));

    return false;
}
