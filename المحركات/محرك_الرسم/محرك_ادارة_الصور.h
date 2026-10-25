#ifndef IMAGE_MANAGER_H
#define IMAGE_MANAGER_H

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

/* ─────────────────────────────────────────────
   PNG Support Functions
───────────────────────────────────────────── */

/* Public Functions */
EFI_STATUS DrawPNG(
    EFI_FILE_HANDLE Root,
    CHAR16 *FileName,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);

/* Load PNG frame from disk */
UINT8 *load_png_frame(
    EFI_FILE_HANDLE Root,
    CHAR16 *Path,
    UINT32 *outW,
    UINT32 *outH);

/* BitReader structure for DEFLATE decompression
 * (نافذة بتات مجمّعة UINT32 بدل قراءة بت واحد كل مرة - أسرع بكثير) */
typedef struct {
    const UINT8 *src;
    UINTN        srcLen;
    UINTN        srcPos;
    UINT32       window;     /* البتات المجمّعة (LSB = أول بت غير مستهلك) */
    UINT32       windowBits; /* عدد البتات الصالحة داخل window */
} BitReader;

#endif /* IMAGE_MANAGER_H */
