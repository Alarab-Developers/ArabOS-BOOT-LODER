#include "محمل_النواه.h"

#include <string.h>

EFI_STATUS kernel_load(
    EFI_FILE_HANDLE Root,
    CHAR16* Path
)
{
    EFI_STATUS Status;
    EFI_FILE_HANDLE File;

    Print(L"\n");
    Print(L"=============================\n");
    Print(L"[DEBUG] kernel_load()\n");
    Print(L"[DEBUG] Path: %s\n", Path);
    Print(L"=============================\n");

    /*
     * فتح الملف
     */

    Status =
        uefi_call_wrapper(
            Root->Open,
            5,
            Root,
            &File,
            Path,
            EFI_FILE_MODE_READ,
            0
        );

    if (EFI_ERROR(Status))
    {
        Print(L"[ERROR] Open failed: %r\n", Status);
        return Status;
    }

    Print(L"[DEBUG] File opened\n");

    /*
     * قراءة الترويسة
     */

    aros_kernel_header_t hdr;

    UINTN size =
        sizeof(aros_kernel_header_t);

    Status =
        uefi_call_wrapper(
            File->Read,
            3,
            File,
            &size,
            &hdr
        );

    if (EFI_ERROR(Status))
    {
        Print(L"[ERROR] Header read failed: %r\n", Status);

        uefi_call_wrapper(
            File->Close,
            1,
            File
        );

        return Status;
    }

    Print(L"[DEBUG] Header read OK\n");

    /*
     * عرض بيانات الترويسة
     */

    Print(L"[DEBUG] Format      : %a\n", hdr.format);
    Print(L"[DEBUG] Entry Point : 0x%lx\n", hdr.entry_point);
    Print(L"[DEBUG] Image Size  : %lu bytes\n", hdr.image_size);
    Print(L"[DEBUG] Flags       : %lu\n", hdr.flags);

    /*
     * التحقق من الصيغة
     */

    if (strcmp(hdr.format, KERNEL_FORMAT) != 0)
    {
        Print(L"[ERROR] Invalid kernel format\n");

        uefi_call_wrapper(
            File->Close,
            1,
            File
        );

        return EFI_LOAD_ERROR;
    }

    Print(L"[DEBUG] Format OK\n");

    /*
     * التحقق من الحجم
     */

    if (hdr.image_size == 0)
    {
        Print(L"[ERROR] Image size is zero\n");

        uefi_call_wrapper(
            File->Close,
            1,
            File
        );

        return EFI_LOAD_ERROR;
    }

    /*
     * حجز الذاكرة
     */

    UINTN pages =
        EFI_SIZE_TO_PAGES(
            hdr.image_size
        );

    EFI_PHYSICAL_ADDRESS addr = 0;

    Status =
        uefi_call_wrapper(
            BS->AllocatePages,
            4,
            AllocateAnyPages,
            EfiLoaderData,
            pages,
            &addr
        );

    if (EFI_ERROR(Status))
    {
        Print(L"[ERROR] AllocatePages failed: %r\n", Status);

        uefi_call_wrapper(
            File->Close,
            1,
            File
        );

        return Status;
    }

    Print(L"[DEBUG] Memory allocated\n");
    Print(L"[DEBUG] Address = 0x%lx\n", addr);
    Print(L"[DEBUG] Pages   = %lu\n", pages);

    /*
     * تحميل صورة النواة
     */

    size = hdr.image_size;

    Status =
        uefi_call_wrapper(
            File->Read,
            3,
            File,
            &size,
            (void*)addr
        );

    if (EFI_ERROR(Status))
    {
        Print(L"[ERROR] Kernel image read failed: %r\n", Status);

        uefi_call_wrapper(
            File->Close,
            1,
            File
        );

        return Status;
    }

    Print(L"[DEBUG] Kernel image loaded\n");
    Print(L"[DEBUG] Loaded bytes = %lu\n", size);

    /*
     * حساب نقطة الدخول
     */

    kernel_entry_t entry =
        (kernel_entry_t)
        (
            (uint8_t*)addr +
            hdr.entry_point
        );

    Print(L"[DEBUG] Entry address = 0x%lx\n", entry);

    /*
     * الحصول على GOP
     */

    EFI_GUID GopGuid =
        EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;

    Status =
        uefi_call_wrapper(
            BS->LocateProtocol,
            3,
            &GopGuid,
            NULL,
            (void**)&gop
        );

    if (EFI_ERROR(Status))
    {
        Print(L"[ERROR] GOP not found: %r\n", Status);

        uefi_call_wrapper(
            File->Close,
            1,
            File
        );

        return Status;
    }

    Print(L"[DEBUG] GOP found\n");

    /*
     * معلومات الإقلاع
     */

    boot_info_t boot;

    boot.FrameBuffer =
        (void*)gop->Mode->FrameBufferBase;

    boot.Width =
        gop->Mode->Info->HorizontalResolution;

    boot.Height =
        gop->Mode->Info->VerticalResolution;

    boot.PixelsPerScanLine =
        gop->Mode->Info->PixelsPerScanLine;

    Print(L"[DEBUG] FrameBuffer = 0x%lx\n",
          gop->Mode->FrameBufferBase);

    Print(L"[DEBUG] Resolution = %ux%u\n",
          boot.Width,
          boot.Height);

    Print(L"[DEBUG] PixelsPerScanLine = %u\n",
          boot.PixelsPerScanLine);

    /*
     * إغلاق الملف
     */

    uefi_call_wrapper(
        File->Close,
        1,
        File
    );

    Print(L"\n");
    Print(L"[DEBUG] Ready to jump to kernel\n");
    Print(L"[DEBUG] Press any key...\n");

    EFI_INPUT_KEY Key;

    while (
        EFI_ERROR(
            uefi_call_wrapper(
                ST->ConIn->ReadKeyStroke,
                2,
                ST->ConIn,
                &Key
            )
        )
    );

    /*
     * تشغيل النواة
     */

    entry(&boot);

    Print(L"[ERROR] Kernel returned!\n");

    return EFI_SUCCESS;
}
