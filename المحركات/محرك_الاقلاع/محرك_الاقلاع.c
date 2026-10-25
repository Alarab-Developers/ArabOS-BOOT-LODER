#include "محرك_الاقلاع.h"

/* ─────────────────────────────────────────────
   SetPreferredMode  –  يضبط دقة محددة (مثلاً 1280×720)
   إن لم تكن متاحة يبقى على الدقة الحالية بدون خطأ.
───────────────────────────────────────────── */
EFI_STATUS SetPreferredMode(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
    UINT32 wantW,
    UINT32 wantH)
{
    UINT32 totalModes = gop->Mode->MaxMode;

    for (UINT32 i = 0; i < totalModes; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
        UINTN infoSize = 0;

        EFI_STATUS st = uefi_call_wrapper(
            gop->QueryMode, 4, gop, i, &infoSize, &info);
        if (EFI_ERROR(st) || !info) continue;

        BOOLEAN match =
            (info->HorizontalResolution == wantW &&
             info->VerticalResolution   == wantH);
        FreePool(info);

        if (match) {
            return uefi_call_wrapper(gop->SetMode, 2, gop, i);
        }
    }

    /* الدقة المطلوبة غير متاحة – نبقى على الحالية */
    return EFI_SUCCESS;
}
