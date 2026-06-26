#ifndef KERNEL_LOADER_H
#define KERNEL_LOADER_H

#include <efi.h>
#include <efilib.h>
#include <stdint.h>

/* ─────────────────────────────────────────────────────
   يجب أن يتطابق مع `. = 0x100000` في linker.ld
────────────────────────────────────────────────────── */
#define KERNEL_LOAD_ADDRESS  0x100000ULL

#define KERNEL_FORMAT "[تطبيق]"

typedef struct
{
    char     format[128];
    uint64_t entry_point;   /* offset من بداية الصورة (ليس عنوان مطلق) */
    uint64_t image_size;    /* حجم الصورة الكاملة بعد الترويسة */
    uint64_t flags;
} aros_kernel_header_t;

typedef struct
{
    void   *FrameBuffer;
    UINT32  Width;
    UINT32  Height;
    UINT32  PixelsPerScanLine;
} boot_info_t;

typedef void (*kernel_entry_t)(boot_info_t *);

/*
 * kernel_load
 * ───────────
 * ImageHandle : يجب تمريره من efi_main لاستخدامه في ExitBootServices
 * Root        : مجلد الجذر المفتوح مسبقاً
 * Path        : مسار ملف النواة (.ن)
 */
EFI_STATUS kernel_load(
    EFI_HANDLE      ImageHandle,
    EFI_FILE_HANDLE Root,
    CHAR16         *Path);

#endif /* KERNEL_LOADER_H */
