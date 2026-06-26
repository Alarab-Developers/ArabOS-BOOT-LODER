#include "محمل_النواه.h"
#include "محرك_الذاكرة/محرك_الذاكرة.h"


EFI_STATUS kernel_load(
    EFI_HANDLE      ImageHandle,
    EFI_FILE_HANDLE Root,
    CHAR16         *Path)
{
    EFI_STATUS Status;
    EFI_FILE_HANDLE File;

    //Print(L"\n=============================\n");
    //Print(L"[LOADER] kernel_load()\n");
    //Print(L"[LOADER] Path: %s\n", Path);
    //Print(L"=============================\n");

    /* ─────────────────────────
       1. فتح الملف
    ───────────────────────── */
    Status = uefi_call_wrapper(
        Root->Open, 5,
        Root, &File, Path,
        EFI_FILE_MODE_READ, 0);

    if (EFI_ERROR(Status)) {
        //Print(L"[ERROR] Open failed: %r\n", Status);
        return Status;
    }
    //Print(L"[LOADER] File opened OK\n");

    /* ─────────────────────────
       2. قراءة الترويسة
    ───────────────────────── */
    aros_kernel_header_t hdr;
    UINTN size = sizeof(aros_kernel_header_t);

    Status = uefi_call_wrapper(
        File->Read, 3, File, &size, &hdr);

    if (EFI_ERROR(Status)) {
        //Print(L"[ERROR] Header read: %r\n", Status);
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    //Print(L"[LOADER] Format     : %a\n",   hdr.format);
    //Print(L"[LOADER] EntryOff   : 0x%lx\n", hdr.entry_point);
    //Print(L"[LOADER] ImageSize  : %lu\n",   hdr.image_size);

    //Print(L"[DBG] A - before format check\n");

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
        //Print(L"[ERROR] Bad format\n");
        uefi_call_wrapper(File->Close, 1, File);
        return EFI_LOAD_ERROR;
    }

    //Print(L"[DBG] B - after format check\n");

    if (hdr.image_size == 0 || hdr.image_size > 64*1024*1024) {
        //Print(L"[ERROR] Bad image size\n");
        uefi_call_wrapper(File->Close, 1, File);
        return EFI_LOAD_ERROR;
    }

    //Print(L"[DBG] C - after size check\n");

    /* ─────────────────────────
       4. حجز الذاكرة على 0x100000 بالضبط
       (النواة مُجمَّعة على هذا العنوان — لا يجوز تغييره)
    ───────────────────────── */
    UINTN pages = EFI_SIZE_TO_PAGES(hdr.image_size);

    //Print(L"[DBG] pages=%lu\n", pages);

    EFI_PHYSICAL_ADDRESS addr = KERNEL_LOAD_ADDRESS;

    //Print(L"[DBG] Before AllocatePages @ 0x%lx\n", addr);

    Status = uefi_call_wrapper(
        BS->AllocatePages, 4,
        AllocateAddress,      /* ثابت على العنوان المطلوب */
        EfiLoaderData,
        pages,
        &addr);

    //Print(L"[DBG] After AllocatePages: %r\n", Status);

    if (EFI_ERROR(Status)) {
        //Print(L"[ERROR] AllocatePages @ 0x%lx failed: %r\n",
         //     KERNEL_LOAD_ADDRESS, Status);
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    //Print(L"[LOADER] Allocated @ 0x%lx\n", addr);

    /* ─────────────────────────
       5. تحميل الصورة
    ───────────────────────── */
    size = (UINTN)hdr.image_size;

    Status = uefi_call_wrapper(
        File->Read, 3, File, &size, (void *)addr);

    if (EFI_ERROR(Status)) {
        //Print(L"[ERROR] Read kernel: %r\n", Status);
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    //Print(L"[LOADER] Loaded %lu bytes\n", size);
    uefi_call_wrapper(File->Close, 1, File);

    /* ─────────────────────────
       6. نقطة الدخول
    ───────────────────────── */
    kernel_entry_t entry =
        (kernel_entry_t)(addr + hdr.entry_point);

    //Print(L"[LOADER] Entry @ 0x%lx\n", (UINT64)entry);

    /* ─────────────────────────
       7. GOP
    ───────────────────────── */
    EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;

    Status = uefi_call_wrapper(
        BS->LocateProtocol, 3,
        &GopGuid, NULL, (void **)&gop);

    if (EFI_ERROR(Status)) {
        //Print(L"[ERROR] GOP: %r\n", Status);
        return Status;
    }

    boot_info_t boot;
    boot.FrameBuffer       = (void *)gop->Mode->FrameBufferBase;
    boot.Width             = gop->Mode->Info->HorizontalResolution;
    boot.Height            = gop->Mode->Info->VerticalResolution;
    boot.PixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;

    //Print(L"[LOADER] FB=0x%lx %ux%u pitch=%u\n",
        //  (UINT64)boot.FrameBuffer,
         // boot.Width, boot.Height,
        //  boot.PixelsPerScanLine);

    //Print(L"\n[LOADER] press any key to continue...\n");

    //EFI_INPUT_KEY Key;
    //uefi_call_wrapper(ST->ConIn->Reset, 2, ST->ConIn, FALSE);
    //while (EFI_ERROR(
    //    uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key)))
    //{
    //}

    /* ─────────────────────────
       إنشاء جداول الصفحات
       (next_page يبدأ من 0x200000 — بعد النواة)
    ───────────────────────── */
    EFI_STATUS mst = memory_init(BS);

    if (EFI_ERROR(mst)) {
        //Print(L"[ERROR] memory_init: %r\n", mst);
        for (;;);
    }

    paging_build_identity();

    //Print(L"[MM] PML4     = %lx\n", (UINT64)g_memory.pml4);
    //Print(L"[MM] PDPT_LOW = %lx\n", (UINT64)g_memory.pdpt_low);

    //for (int i = 0; i < 4; i++)
        //Print(L"[MM] PD[%d]   = %lx\n", i, (UINT64)g_memory.pd_low[i]);

    //Print(L"[MM] PML4[0]  = %lx\n", g_memory.pml4[0]);
    //Print(L"[LOADER] EntryOff = 0x%lx\n", hdr.entry_point);

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
