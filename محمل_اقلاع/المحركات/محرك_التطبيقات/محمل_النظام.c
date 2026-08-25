#include "محمل_النظام.h"

EFI_STATUS load_system_archive(
    EFI_FILE_HANDLE Root,
    CHAR16         *Path,
    boot_info_t *boot)
{
    EFI_STATUS Status;
    EFI_FILE_HANDLE File;

    /* إذا لم يُحدَّد مسار في ملف الإعدادات، نستخدم الاسم الافتراضي القديم */
    CHAR16 *archivePath = (Path && Path[0]) ? Path : L"sys.tar";

    Status = uefi_call_wrapper(
        Root->Open,
        5,
        Root,
        &File,
        archivePath,
        EFI_FILE_MODE_READ,
        0);

    if (EFI_ERROR(Status))
        return Status;

    UINTN info_size =
        SIZE_OF_EFI_FILE_INFO + 256;

    EFI_FILE_INFO *info;

    Status = uefi_call_wrapper(
        BS->AllocatePool,
        3,
        EfiLoaderData,
        info_size,
        (void **)&info);

    if (EFI_ERROR(Status))
    {
        uefi_call_wrapper(File->Close,1,File);
        return Status;
    }

    EFI_GUID guid = EFI_FILE_INFO_ID;

    Status = uefi_call_wrapper(
        File->GetInfo,
        4,
        File,
        &guid,
        &info_size,
        info);

    if (EFI_ERROR(Status))
    {
        uefi_call_wrapper(File->Close,1,File);
        uefi_call_wrapper(BS->FreePool,1,info);
        return Status;
    }

    UINTN archive_size =
        (UINTN)info->FileSize;

    EFI_PHYSICAL_ADDRESS address = 0;

    Status = uefi_call_wrapper(
        BS->AllocatePages,
        4,
        AllocateAnyPages,
        EfiLoaderData,
        EFI_SIZE_TO_PAGES(archive_size),
        &address);

    if (EFI_ERROR(Status))
    {
        uefi_call_wrapper(File->Close,1,File);
        uefi_call_wrapper(BS->FreePool,1,info);
        return Status;
    }

    UINTN read_size = archive_size;

    Print(L"[SYS] Opening %s...\n", archivePath);

    Status = uefi_call_wrapper(
        File->Read,
        3,
        File,
        &read_size,
        (void *)address);

    uefi_call_wrapper(File->Close,1,File);
    uefi_call_wrapper(BS->FreePool,1,info);

    if (EFI_ERROR(Status))
    {
        Print(L"[SYS] Open failed: %r\n", Status);
    
        return Status;
    }
    Print(L"[SYS] File opened successfully\n");

    boot->system_archive =
        (void *)address;
    Print(L"[SYS] File size = %lu bytes\n", archive_size);

    boot->system_archive_size =
        archive_size;
    Print(L"[SYS] File size = %lu bytes\n", archive_size);

    return EFI_SUCCESS;
}
