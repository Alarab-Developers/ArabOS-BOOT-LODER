#include "محمل_النواه.h"
#include "محمل_النظام.h"
#include "محرك_الذاكرة/محرك_الذاكرة.h"


/*
 * compute_total_ram
 * -----------------
 * تمر على خريطة الذاكرة القادمة من UEFI GetMemoryMap وتحسب أعلى
 * عنوان فيزيائي ضمن المناطق القابلة للاستخدام، بدل الاعتماد على
 * قيمة ثابتة (64MB) داخل النواة.
 */
static uint64_t compute_total_ram(EFI_MEMORY_DESCRIPTOR *map, UINTN mapSize, UINTN descSize)
{
    uint64_t max_addr = 0;
    UINT8 *p = (UINT8 *)map;
    UINTN count = mapSize / descSize;

    for (UINTN i = 0; i < count; i++) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)(p + i * descSize);

        switch (d->Type) {
            case EfiConventionalMemory:
            case EfiBootServicesCode:
            case EfiBootServicesData:
            case EfiLoaderCode:
            case EfiLoaderData:
            case EfiACPIReclaimMemory: {
                uint64_t end = d->PhysicalStart + d->NumberOfPages * EFI_PAGE_SIZE;
                if (end > max_addr)
                    max_addr = end;
                break;
            }
            default:
                break;
        }
    }

    return max_addr;
}

EFI_STATUS kernel_load(
    EFI_HANDLE      ImageHandle,
    EFI_FILE_HANDLE Root,
    CHAR16         *Path,
    CHAR16         *SysArchivePath)
{
    EFI_STATUS Status;
    EFI_FILE_HANDLE File;

    /* ─────────────────────────
       1. فتح الملف
    ───────────────────────── */
    Status = uefi_call_wrapper(
        Root->Open, 5,
        Root, &File, Path,
        EFI_FILE_MODE_READ, 0);

    if (EFI_ERROR(Status)) {
        return Status;
    }

    /* ─────────────────────────
       2. قراءة الترويسة
    ───────────────────────── */
    aros_kernel_header_t hdr;
    UINTN size = sizeof(aros_kernel_header_t);

    Status = uefi_call_wrapper(
        File->Read, 3, File, &size, &hdr);

    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    /* ─────────────────────────
       3. التحقق
    ───────────────────────── */
    BOOLEAN format_ok = TRUE;
    {
        const char *a = hdr.format;
        const char *b = KERNEL_FORMAT;
        while (*a && *b) {
            if (*a != *b) { format_ok = FALSE; break; }
            a++; b++;
        }
        if (format_ok && *a != *b) format_ok = FALSE;
    }

    if (!format_ok) {
        uefi_call_wrapper(File->Close, 1, File);
        return EFI_LOAD_ERROR;
    }

    if (hdr.image_size == 0 || hdr.image_size > 64*1024*1024) {
        uefi_call_wrapper(File->Close, 1, File);
        return EFI_LOAD_ERROR;
    }

    /* ─────────────────────────
       4. حجز الذاكرة على 0x100000 بالضبط
    ───────────────────────── */
    UINTN pages = EFI_SIZE_TO_PAGES(hdr.image_size);

    EFI_PHYSICAL_ADDRESS addr = KERNEL_LOAD_ADDRESS;

    Status = uefi_call_wrapper(
        BS->AllocatePages, 4,
        AllocateAddress,
        EfiLoaderData,
        pages,
        &addr);

    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    /* ─────────────────────────
       4.5 تصفير الذاكرة المحجوزة
    ───────────────────────── */
    SetMem((void *)addr, pages * EFI_PAGE_SIZE, 0);

    /* ─────────────────────────
       5. تحميل الصورة
    ───────────────────────── */
    size = (UINTN)hdr.image_size;

    Status = uefi_call_wrapper(
        File->Read, 3, File, &size, (void *)addr);

    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    uefi_call_wrapper(File->Close, 1, File);

    /* ─────────────────────────
       6. نقطة الدخول
    ───────────────────────── */
    kernel_entry_t entry =
        (kernel_entry_t)(addr + hdr.entry_point);

    /* ─────────────────────────
       7. GOP
    ───────────────────────── */
    EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;

    Status = uefi_call_wrapper(
        BS->LocateProtocol, 3,
        &GopGuid, NULL, (void **)&gop);

    if (EFI_ERROR(Status)) {
        return Status;
    }

    boot_info_t boot;
    boot.FrameBuffer       = (void *)gop->Mode->FrameBufferBase;
    boot.Width             = gop->Mode->Info->HorizontalResolution;
    boot.Height            = gop->Mode->Info->VerticalResolution;
    boot.PixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;

    Status = load_system_archive(
        Root,
        SysArchivePath,
        &boot);

    if (EFI_ERROR(Status))
        return Status;

    /* ─────────────────────────
       إنشاء جداول الصفحات
       ⬇⬇⬇ التعديل هنا ⬇⬇⬇
       نمرّر نهاية الصورة الفعلية للنواة إلى memory_init
       حتى تبدأ جداول الصفحات بعدها، لا عند 0x200000 الثابت.
    ───────────────────────── */
    uint64_t kernel_image_end =
        KERNEL_LOAD_ADDRESS + (uint64_t)hdr.image_size;

    EFI_STATUS mst = memory_init(BS, kernel_image_end);

    if (EFI_ERROR(mst)) {
        for (;;);
    }

    paging_build_identity();

    /* ─────────────────────────
       8. Memory Map + ExitBootServices
    ───────────────────────── */
    UINTN MapSize  = 0;
    UINTN MapKey   = 0;
    UINTN DescSize = 0;
    UINT32 DescVer = 0;
    EFI_MEMORY_DESCRIPTOR *MemMap = NULL;

    uefi_call_wrapper(
        BS->GetMemoryMap, 5,
        &MapSize, NULL, &MapKey, &DescSize, &DescVer);

    MapSize += 4 * DescSize;

    Status = uefi_call_wrapper(
        BS->AllocatePool, 3,
        EfiLoaderData, MapSize, (void **)&MemMap);

    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(
        BS->GetMemoryMap, 5,
        &MapSize, MemMap, &MapKey, &DescSize, &DescVer);

    if (EFI_ERROR(Status)) return Status;

    boot.total_ram_bytes = compute_total_ram(MemMap, MapSize, DescSize);

    Status = uefi_call_wrapper(
        BS->ExitBootServices, 2,
        ImageHandle, MapKey);

    if (EFI_ERROR(Status)) {
        MapSize += 4 * DescSize;
        Status = uefi_call_wrapper(
            BS->GetMemoryMap, 5,
            &MapSize, MemMap, &MapKey, &DescSize, &DescVer);

        if (!EFI_ERROR(Status)) {
            Status = uefi_call_wrapper(
                BS->ExitBootServices, 2,
                ImageHandle, MapKey);
        }

        if (EFI_ERROR(Status)) return Status;
    }

    /* ══════════════════════════════════════
       Bare metal من هنا
    ══════════════════════════════════════ */
    {
        volatile UINT32 *fb = (volatile UINT32 *)boot.FrameBuffer;
        UINTN total = (UINTN)boot.Height * (UINTN)boot.PixelsPerScanLine;
        for (UINTN i = 0; i < total; i++)
            fb[i] = 0;
    }

    paging_enable_kernel_tables();

    entry(&boot);

    for (;;) __asm__ __volatile__("cli; hlt");
    return EFI_SUCCESS;
}
