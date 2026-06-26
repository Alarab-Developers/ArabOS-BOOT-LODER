
#include "محرك_التمهيد.h"
#include "محرك_الرسم/محرك_ادارة_الصور.h"

/* ─────────────────────────────────────────────
   ثوابت الأداء
───────────────────────────────────────────── */
#define ANIMATION_QUICK_MODE 0   /* إيقاف الوضع السريع */
#define ANIMATION_MAX_FRAMES_SLOW 8
#define ANIMATION_MAX_FRAMES_FAST 15
#define ANIMATION_LOOP_COUNT 3    /* عدد مرات تكرار الأنيميشن */

/* ─────────────────────────────────────────────
   render_frame_fast - رسم سريع
───────────────────────────────────────────── */
void render_frame_fast(const UINT8 *rgba, UINT32 imgW, UINT32 imgH,
                       EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    UINT32 *fb    = (UINT32 *)gop->Mode->FrameBufferBase;
    UINTN   scrW  = gop->Mode->Info->HorizontalResolution;
    UINTN   scrH  = gop->Mode->Info->VerticalResolution;
    UINTN   pitch = gop->Mode->Info->PixelsPerScanLine;
    
    UINTN offX = (scrW > imgW) ? (scrW - imgW) / 2 : 0;
    UINTN offY = (scrH > imgH) ? (scrH - imgH) / 2 : 0;
    UINTN drawW = (imgW < scrW - offX) ? imgW : (scrW - offX);
    UINTN drawH = (imgH < scrH - offY) ? imgH : (scrH - offY);
    
    for (UINTN y = 0; y < drawH; y++) {
        const UINT8 *row = rgba + y * imgW * 4;
        UINT32 *dst = fb + (offY + y) * pitch + offX;
        
        for (UINTN x = 0; x < drawW; x++) {
            UINT32 pixel = ((UINT32)row[x * 4 + 0] << 16) |
                           ((UINT32)row[x * 4 + 1] << 8)  |
                           ((UINT32)row[x * 4 + 2]);
            dst[x] = pixel;
        }
    }
}

/* ─────────────────────────────────────────────
   clear_screen - مسح الشاشة بالأسود
───────────────────────────────────────────── */
void clear_screen(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    UINT32 *fb    = (UINT32 *)gop->Mode->FrameBufferBase;
    UINTN   scrH  = gop->Mode->Info->VerticalResolution;
    UINTN   pitch = gop->Mode->Info->PixelsPerScanLine;
    UINTN   total_pixels = scrH * pitch;
    
    SetMem(fb, total_pixels * sizeof(UINT32), 0);
}

/* ─────────────────────────────────────────────
   stall_us – انتظار
───────────────────────────────────────────── */
void stall_us(UINT32 us)
{
    if (us > 0) {
        uefi_call_wrapper(BS->Stall, 1, (UINTN)us);
    }
}

/* ─────────────────────────────────────────────
   build_path – بناء مسار الإطار
───────────────────────────────────────────── */
static void build_path(CHAR16 *path, CHAR16 *FolderName, UINT32 frameNum)
{
    UINTN fi = 0;
    while (FolderName[fi]) { 
        path[fi] = FolderName[fi]; 
        fi++; 
    }
    path[fi++] = L'\\';
    
    CHAR16 digits[12];
    INT32 dc = 0;
    UINT32 n = frameNum;
    do { 
        digits[dc++] = (CHAR16)(L'0' + n % 10); 
        n /= 10; 
    } while (n);
    
    for (INT32 d = dc - 1; d >= 0; d--) {
        path[fi++] = digits[d];
    }
    
    path[fi++] = L'.';
    path[fi++] = L'p';
    path[fi++] = L'n';
    path[fi++] = L'g';
    path[fi] = L'\0';
}



static void render_frame_alpha(
    const UINT8 *rgba,
    UINT32 imgW,
    UINT32 imgH,
    UINT8 alpha,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    UINT32 *fb = (UINT32*)gop->Mode->FrameBufferBase;

    UINTN scrW  = gop->Mode->Info->HorizontalResolution;
    UINTN scrH  = gop->Mode->Info->VerticalResolution;
    UINTN pitch = gop->Mode->Info->PixelsPerScanLine;

    UINTN offX = (scrW > imgW) ? (scrW - imgW) / 2 : 0;
    UINTN offY = (scrH > imgH) ? (scrH - imgH) / 2 : 0;

    for (UINTN y = 0; y < imgH; y++)
    {
        const UINT8 *row = rgba + y * imgW * 4;
        UINT32 *dst = fb + (offY + y) * pitch + offX;

        for (UINTN x = 0; x < imgW; x++)
        {
            UINT8 r = row[x*4+0];
            UINT8 g = row[x*4+1];
            UINT8 b = row[x*4+2];

            r = (r * alpha) / 255;
            g = (g * alpha) / 255;
            b = (b * alpha) / 255;

            dst[x] =
                ((UINT32)r << 16) |
                ((UINT32)g << 8)  |
                 (UINT32)b;
        }
    }
}
EFI_STATUS PlayLogoAnimation(
    EFI_FILE_HANDLE Root,
    CHAR16 *FolderName,
    UINT32  FrameCount,
    UINT32  FrameDelay,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    EFI_STATUS Status = EFI_SUCCESS;

    if (!Root || !FolderName || !gop || FrameCount == 0)
        return EFI_INVALID_PARAMETER;

    UINT32 actualFrameCount = FrameCount;
    if (actualFrameCount > 30)
        actualFrameCount = 30;

    typedef struct {
        UINT8  *rgba;
        UINT32  w;
        UINT32  h;
    } FrameEntry;

    FrameEntry *frameTable =
        AllocateZeroPool(actualFrameCount * sizeof(FrameEntry));

    if (!frameTable)
        return EFI_OUT_OF_RESOURCES;

    CHAR16 path[512];
    UINT32 loadedCount = 0;

    /* تحميل جميع الإطارات */
    for (UINT32 i = 0;
         i < actualFrameCount && loadedCount < actualFrameCount;
         i++)
    {
        build_path(path, FolderName, i + 1);

        UINT32 fw = 0;
        UINT32 fh = 0;

        UINT8 *rgba =
            load_png_frame(Root, path, &fw, &fh);

        if (!rgba)
            continue;

        frameTable[loadedCount].rgba = rgba;
        frameTable[loadedCount].w    = fw;
        frameTable[loadedCount].h    = fh;

        loadedCount++;
    }

    if (loadedCount == 0)
    {
        FreePool(frameTable);
        return EFI_NOT_FOUND;
    }

    clear_screen(gop);

    UINT32 fadeFrames = 15;

    if (fadeFrames > loadedCount)
        fadeFrames = loadedCount;

    for (UINT32 loop = 0;
         loop < ANIMATION_LOOP_COUNT;
         loop++)
    {
        for (UINT32 i = 0;
             i < loadedCount;
             i++)
        {
            UINT8 alpha = 255;

            /* Fade In */
            if (loop == 0)
            {
                if (i < fadeFrames)
                {
                    alpha =
                        (UINT8)((255 * (i + 1))
                        / fadeFrames);
                }
            }

            /* Fade Out */
            if (loop == (ANIMATION_LOOP_COUNT - 1))
            {
                UINT32 remain =
                    loadedCount - i;

                if (remain <= fadeFrames)
                {
                    alpha =
                        (UINT8)((255 * remain)
                        / fadeFrames);
                }
            }

            render_frame_alpha(
                frameTable[i].rgba,
                frameTable[i].w,
                frameTable[i].h,
                alpha,
                gop
            );

            stall_us(FrameDelay);
        }

        if (loop < (ANIMATION_LOOP_COUNT - 1))
        {
            stall_us(50000);
        }
    }

    clear_screen(gop);

    for (UINT32 i = 0; i < loadedCount; i++)
    {
        if (frameTable[i].rgba)
            FreePool(frameTable[i].rgba);
    }

    FreePool(frameTable);

    return Status;
}

/* وظيفة مساعدة */
EFI_STATUS ShowSingleFrame(EFI_FILE_HANDLE Root, CHAR16 *FilePath, 
                           EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    UINT32 w = 0, h = 0;
    UINT8 *rgba = load_png_frame(Root, FilePath, &w, &h);
    if (!rgba) return EFI_NOT_FOUND;
    
    clear_screen(gop);
    render_frame_fast(rgba, w, h, gop);
    
    FreePool(rgba);
    return EFI_SUCCESS;
}
