#include "محرك_الذاكرة.h"
#include <efilib.h>

memory_context_t g_memory;

static EFI_BOOT_SERVICES* g_bs = NULL;

/*
 * next_page يبدأ من 0x200000 (بعد النواة بأمان).
 * النواة محمَّلة على 0x100000 وقد تمتد حتى ~0x200000.
 * جداول الصفحات لا تتجاوز 6 صفحات = 24KB.
 */
static EFI_PHYSICAL_ADDRESS next_page = 0x200000;

static void* alloc_page(void)
{
    EFI_PHYSICAL_ADDRESS addr = next_page;
    next_page += 0x1000;

    EFI_STATUS st =
        uefi_call_wrapper(
            g_bs->AllocatePages,
            4,
            AllocateAddress,
            EfiLoaderData,
            1,
            &addr);

    if (EFI_ERROR(st))
        return NULL;

    SetMem((void*)addr, 4096, 0);

    Print(L"page=%lx\n", addr);

    return (void*)addr;
}

EFI_STATUS memory_init(EFI_BOOT_SERVICES* bs)
{
    g_bs = bs;

    g_memory.pml4      = alloc_page();
    g_memory.pdpt_low  = alloc_page();

    if (!g_memory.pml4 ||
        !g_memory.pdpt_low)
    {
        return EFI_OUT_OF_RESOURCES;
    }

    for (int i = 0; i < 4; i++)
    {
        g_memory.pd_low[i] = alloc_page();

        if (!g_memory.pd_low[i])
            return EFI_OUT_OF_RESOURCES;
    }

    return EFI_SUCCESS;
}

void paging_build_identity(void)
{
    g_memory.pml4[0] =
        ((uint64_t)g_memory.pdpt_low) | 0x03;

    for (int pdpt = 0; pdpt < 4; pdpt++)
    {
        g_memory.pdpt_low[pdpt] =
            ((uint64_t)g_memory.pd_low[pdpt]) | 0x03;
    }

    for (int pdpt = 0; pdpt < 4; pdpt++)
    {
        for (uint64_t i = 0; i < 512; i++)
        {
            uint64_t phys =
                (((uint64_t)pdpt * 512ULL) + i)
                << 21;

            g_memory.pd_low[pdpt][i] =
                phys | 0x83;
        }
    }
}

static inline void load_cr3(uint64_t value)
{
    __asm__ __volatile__(
        "mov %0, %%cr3"
        :
        : "r"(value)
        : "memory");
}

void paging_enable_kernel_tables(void)
{
    load_cr3((uint64_t)g_memory.pml4);
}
