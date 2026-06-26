#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

/* ─────────────────────────────────────────────
   دعم المسارات العربية – UTF-8 → UCS-2
   ─────────────────────────────────────────────
   FAT32/UEFI يعمل بـ CHAR16 (UCS-2).
   نُحوّل السلاسل العربية (UTF-8) إلى UCS-2
   بحيث يمكن فتح الملفات بأسماء عربية مباشرةً.
─────────────────────────────────────────────── */

/*
 * Utf8ToUcs2
 * ----------
 * تحوّل سلسلة UTF-8 (مؤشر CHAR8) إلى سلسلة UCS-2 (مؤشر CHAR16).
 * dst  : المخزن المؤقت للإخراج
 * dstMax: الحجم الأقصى بعدد CHAR16 (شامل المُنهي)
 * src  : سلسلة UTF-8 المُدخَلة (منتهية بـ \0)
 *
 * تتجاهل أي نقطة ترميز لا يمكن تمثيلها في UCS-2 (> U+FFFF).
 * تُعيد عدد الأحرف المكتوبة (بدون المُنهي).
 */
static inline UINTN Utf8ToUcs2(CHAR16 *dst, UINTN dstMax, const CHAR8 *src)
{
    UINTN out = 0;
    if (!dst || dstMax == 0 || !src) return 0;

    while (*src && out < dstMax - 1) {
        UINT8 c = (UINT8)*src;
        UINT32 cp;

        if (c < 0x80) {
            /* ASCII مباشر */
            cp = c;
            src++;
        } else if ((c & 0xE0) == 0xC0) {
            /* نقطتان: U+0080 – U+07FF */
            cp  = (UINT32)(c & 0x1F) << 6;
            src++;
            if (((UINT8)*src & 0xC0) == 0x80)
                cp |= (UINT32)((UINT8)*src++ & 0x3F);
        } else if ((c & 0xF0) == 0xE0) {
            /* ثلاثة بايتات: U+0800 – U+FFFF  (الأبجدية العربية هنا) */
            cp  = (UINT32)(c & 0x0F) << 12;
            src++;
            if (((UINT8)*src & 0xC0) == 0x80)
                cp |= (UINT32)((UINT8)*src++ & 0x3F) << 6;
            if (((UINT8)*src & 0xC0) == 0x80)
                cp |= (UINT32)((UINT8)*src++ & 0x3F);
        } else if ((c & 0xF8) == 0xF0) {
            /* أربعة بايتات: U+10000+ – خارج UCS-2، نتخطاها */
            src++;
            if (((UINT8)*src & 0xC0) == 0x80) src++;
            if (((UINT8)*src & 0xC0) == 0x80) src++;
            if (((UINT8)*src & 0xC0) == 0x80) src++;
            continue;
        } else {
            /* بايت غير صالح */
            src++;
            continue;
        }

        /* نتجاهل Surrogates (D800-DFFF) */
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;
        if (cp > 0xFFFF) continue;

        dst[out++] = (CHAR16)cp;
    }

    dst[out] = L'\0';
    return out;
}

/*
 * ArabicPath
 * ----------
 * ماكرو مساعد يُنشئ CHAR16[] من حرفية UTF-8 عربية.
 * الاستخدام:
 *   CHAR16 buf[128];
 *   ArabicPath(buf, 128, "menu\\خلفية.png");
 *   Root->Open(..., buf, ...);
 */
#define ArabicPath(dst, maxchars, utf8str) \
    Utf8ToUcs2((dst), (maxchars), (const CHAR8 *)(utf8str))

/* BMP Structures */
typedef struct {
    UINT16 Type;
    UINT32 Size;
    UINT16 Reserved1;
    UINT16 Reserved2;
    UINT32 Offset;
} __attribute__((packed)) BMP_FILE_HEADER;

typedef struct {
    UINT32 Size;
    INT32  Width;
    INT32  Height;
    UINT16 Planes;
    UINT16 BitCount;
    UINT32 Compression;
    UINT32 ImageSize;
    INT32  XPelsPerMeter;
    INT32  YPelsPerMeter;
    UINT32 ClrUsed;
    UINT32 ClrImportant;
} __attribute__((packed)) BMP_INFO_HEADER;

/* ثوابت أنيميشن الشعار */
#define LOGO_FRAME_COUNT   35        /* عدد الصور */
#define LOGO_FRAME_W       250       /* عرض كل صورة بالبكسل */
#define LOGO_FRAME_H       250       /* ارتفاع كل صورة بالبكسل */
#define LOGO_FRAME_DELAY   40000     /* تأخير بين الإطارات بالميكروثانية (≈25fps) */

/*
 * الدقة المفضلة للشاشة
 */
#define PREFERRED_W   1920
#define PREFERRED_H   1080

/* Public Functions */
EFI_STATUS SetPreferredMode(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
    UINT32 wantW,
    UINT32 wantH);

#endif
