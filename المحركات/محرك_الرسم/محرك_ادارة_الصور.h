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

/* BitReader structure for DEFLATE decompression */
typedef struct {
    const UINT8 *src;
    UINTN        srcLen;
    UINTN        srcPos;
    UINT32       bits;
    UINT32       bitCount;
} BitReader;

#endif /* IMAGE_MANAGER_H */
