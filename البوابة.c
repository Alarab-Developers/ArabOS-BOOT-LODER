#include "المحركات/محرك_الاقلاع.h"
#include "المحركات/محرك_القائمة.h"
#include "المحركات/محرك_ادارة_الصور.h"
#include "المحركات/محرك_التمهيد.h"
#include "المحركات/محمل_النواه.h"

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

    CHAR16 Background[256]   = {0};
    CHAR16 AnimationDir[256] = {0};

    BootMenu menu;
    Menu_Init(&menu, (const CHAR8 *)"اختر نظام التشغيل", MENU_DEFAULT_TIMEOUT);

    Menu_ReadFromFile(Root, &menu, Background, AnimationDir);

    if (menu.Count == 0) {
        CHAR16 defaultPath[256];
        ArabicPath(defaultPath, 256, "\\EFI\\ArabOS\\النواة.ن");
        Menu_AddEntry(&menu, (const CHAR8 *)"بدء ArabOS", defaultPath, NULL);
    }

    /* ========================================================= */
    /* تحميل صور القائمة (مرة واحدة فقط)                         */
    /* ========================================================= */
    Menu_LoadImages(
        Root,
        &menu,
        menu.MenuBgPath,
        menu.TitleBarPath,
        menu.SelectedPath,
        menu.ArrowPath,
        menu.ProgressPath);
    
    Menu_LoadButtonImages(Root, &menu);
    Menu_LoadScreenBackground(Root, &menu, Background);

    /* ========================================================= */
    /* تشغيل أنيميشن التمهيد (مرة واحدة)                         */
    /* ========================================================= */
    PlayLogoAnimation(Root, AnimationDir, LOGO_FRAME_COUNT, LOGO_FRAME_DELAY, gop);
    clear_screen(gop);

    /* ========================================================= */
    /* الحلقة الرئيسية                                           */
    /* ========================================================= */
    while (TRUE)
{
    /* عرض القائمة */
    UINT32 choice =
        Menu_Run(
            &menu,
            gop,
            SystemTable
        );

    if (choice >= menu.Count)
        continue;

    /* عرض أنيميشن التحميل */
    PlayLogoAnimation(
        Root,
        AnimationDir,
        LOGO_FRAME_COUNT,
        LOGO_FRAME_DELAY,
        gop
    );

    clear_screen(gop);

    /*
     * عرض المسار المختار
     */

    Print(L"\n");
    Print(L"[BOOT] Loading kernel:\n");
    Print(L"%s\n",
          menu.Entries[choice].Path);
    Print(L"\n");

    /*
     * تحميل النواة فقط
     */

    CHAR16 KernelPath[] =
    L"\\EFI\\ArabOS\\النواة.ن";

    Status =
        kernel_load(
            Root,
            KernelPath
    );

    /*
     * إذا رجعت النواة فهناك خطأ
     */

    clear_screen(gop);

    Print(L"\n\n");
    Print(L"========================================\n");
    Print(L"Kernel returned or failed\n");
    Print(L"Status = %r\n", Status);
    Print(L"========================================\n");
    Print(L"\n");
    Print(L"Press any key...\n");

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

    clear_screen(gop);
}
}
