/* محرك_التمهيد.h */
#ifndef BOOT_ANIMATION_H
#define BOOT_ANIMATION_H

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

/* ─────────────────────────────────────────────
   Boot Animation Module
   ───────────────────────────────────────────── */

/* إعدادات الأنيميشن */
#define ANIMATION_LOOP_COUNT 3      /* عدد مرات التكرار */
#define ANIMATION_QUICK_MODE 0      /* إيقاف الوضع السريع */

/* Public Functions */
EFI_STATUS PlayLogoAnimation(
    EFI_FILE_HANDLE Root,
    CHAR16 *FolderName,
    UINT32  FrameCount,
    UINT32  FrameDelay,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);

void clear_screen(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);
void stall_us(UINT32 us);
void render_frame_fast(const UINT8 *rgba, UINT32 imgW, UINT32 imgH, 
                       EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);

#endif /* BOOT_ANIMATION_H */
