#pragma once

#include <efi.h>
#include <stdint.h>


typedef struct
{
    uint64_t* pml4;
    uint64_t* pdpt_low;
    uint64_t* pd_low[4];
} memory_context_t;

extern memory_context_t g_memory;

/* جديد: نمرّر عنوان نهاية النواة الفعلي حتى تبدأ جداول الصفحات بعده،
   بدلًا من عنوان ثابت 0x200000 قد يتداخل مع .bss النواة. */
EFI_STATUS memory_init(EFI_BOOT_SERVICES* bs, uint64_t kernel_end);

void paging_build_identity(void);

void paging_enable_kernel_tables(void);
