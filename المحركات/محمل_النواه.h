#ifndef KERNEL_LOADER_H
#define KERNEL_LOADER_H

#include <efi.h>
#include <efilib.h>
#include <stdint.h>

#define KERNEL_FORMAT "[ن]"

typedef struct
{
    char format[128];

    uint64_t entry_point;

    uint64_t image_size;

    uint64_t flags;

} aros_kernel_header_t;

typedef struct
{
    void* FrameBuffer;
    UINT32 Width;
    UINT32 Height;
    UINT32 PixelsPerScanLine;
} boot_info_t;

typedef void (*kernel_entry_t)(boot_info_t*);



EFI_STATUS kernel_load(
    EFI_FILE_HANDLE Root,
    CHAR16* Path
);

#endif
