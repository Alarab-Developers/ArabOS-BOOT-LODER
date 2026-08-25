#include "المحركات/محرك_الاقلاع/محرك_الاقلاع.h"
#include "المحركات/محرك_الرسم/محرك_القائمة.h"
#include "المحركات/محرك_الرسم/محرك_ادارة_الصور.h"
#include "المحركات/محرك_التطبيقات/محمل_النواه.h"

EFI_STATUS efi_main(
    EFI_HANDLE        ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);

    EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;

    EFI_STATUS Status = uefi_call_wrapper(
        BS->LocateProtocol, 3,
        &GopGuid, NULL, (void **)&gop);
    if (EFI_ERROR(Status)) return Status;

    EFI_LOADED_IMAGE *LoadedImage;
    EFI_GUID LIP = LOADED_IMAGE_PROTOCOL;

    Status = uefi_call_wrapper(
        BS->HandleProtocol, 3,
        ImageHandle, &LIP, (void **)&LoadedImage);
    if (EFI_ERROR(Status)) return Status;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_GUID SFSP = SIMPLE_FILE_SYSTEM_PROTOCOL;

    Status = uefi_call_wrapper(
        BS->HandleProtocol, 3,
        LoadedImage->DeviceHandle, &SFSP, (void **)&FileSystem);
    if (EFI_ERROR(Status)) return Status;

    EFI_FILE_HANDLE Root;
    Status = uefi_call_wrapper(
        FileSystem->OpenVolume, 2,
        FileSystem, &Root);
    if (EFI_ERROR(Status)) return Status;

    CHAR16 Background[256]     = {0};
    CHAR16 SystemArchive[256]  = {0};

    BootMenu menu;
    Menu_Init(
        &menu,
        (const CHAR8 *)"محمل الإقلاع",
        10  /* مهلة = 10 - هذ هي القيمة الافتراضية 10ث سيتم استبدالها من ملف الإعدادات اذا توجد قيمة موجودة فية */
    );

    Menu_ReadFromFile(
        Root,
        &menu,
        Background,
        SystemArchive
    );

    //Print(L"Timeout = %u\n", menu.TimeoutSec);
    //Print(L"Entries = %u\n", menu.Count);

    /* إذا لم توجد مداخل، نضيف مدخل افتراضي */
    if (menu.Count == 0)
    {
        CHAR16 defaultPath[256];



        Menu_AddEntry(
            &menu,
            (const CHAR8 *)"بدء ArabOS",
            defaultPath,
            NULL
        );
    }

    /* تحميل صور القائمة */
    Menu_LoadImages(
        Root,
        &menu,
        menu.MenuBgPath,
        menu.TitleBarPath,
        menu.SelectedPath,
        menu.ArrowPath,
        menu.ProgressPath
    );

    Menu_LoadButtonImages(
        Root,
        &menu
    );

    Menu_LoadScreenBackground(
        Root,
        &menu,
        Background
    );

    /* تجاوز القائمة إذا كانت المهلة = 0 */
    if (menu.TimeoutSec == 0 && menu.Count > 0)
    {
        return kernel_load(
            ImageHandle,
            Root,
            menu.Entries[0].Path,
            SystemArchive
        );
    }

    while (TRUE)
    {
        UINT32 choice =
            Menu_Run(
                &menu,
                gop,
                SystemTable
            );

        if (choice >= menu.Count)
            continue;
        /* تحميل النواة المختارة من القائمة */
        Status = kernel_load(
            ImageHandle,
            Root,
            menu.Entries[choice].Path,
            SystemArchive
        );



        EFI_INPUT_KEY key;

        uefi_call_wrapper(
            SystemTable->ConIn->Reset,
            2,
            SystemTable->ConIn,
            FALSE
        );

        while (
            EFI_ERROR(
                uefi_call_wrapper(
                    SystemTable->ConIn->ReadKeyStroke,
                    2,
                    SystemTable->ConIn,
                    &key
                )
            )
        );

    }

    return EFI_SUCCESS;
}
