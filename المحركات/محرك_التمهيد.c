/* محرك_التمهيد.c - النسخة المعدلة */
#include "محرك_التمهيد.h"
#include "محرك_ادارة_الصور.h"

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

/* ─────────────────────────────────────────────
   prepare_frame_buffer - تحضير الإطار مسبقاً
───────────────────────────────────────────── */
static UINT32* prepare_frame_buffer(const UINT8 *rgba, UINT32 imgW, UINT32 imgH,
                                     EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    UINTN   scrW  = gop->Mode->Info->HorizontalResolution;
    UINTN   scrH  = gop->Mode->Info->VerticalResolution;
    
    UINTN offX = (scrW > imgW) ? (scrW - imgW) / 2 : 0;
    UINTN offY = (scrH > imgH) ? (scrH - imgH) / 2 : 0;
    UINTN drawW = (imgW < scrW - offX) ? imgW : (scrW - offX);
    UINTN drawH = (imgH < scrH - offY) ? imgH : (scrH - offY);
    
    UINT32 *prepared = AllocatePool(drawH * drawW * sizeof(UINT32));
    if (!prepared) return NULL;
    
    for (UINTN y = 0; y < drawH; y++) {
        const UINT8 *row = rgba + y * imgW * 4;
        UINT32 *dst = prepared + y * drawW;
        
        for (UINTN x = 0; x < drawW; x++) {
            dst[x] = ((UINT32)row[x * 4 + 0] << 16) |
                     ((UINT32)row[x * 4 + 1] << 8)  |
                     ((UINT32)row[x * 4 + 2]);
        }
    }
    
    return prepared;
}

/* ─────────────────────────────────────────────
   render_frame_double_buffer - رسم باستخدام Double Buffer
───────────────────────────────────────────── */
static void render_frame_double_buffer(
    const UINT32 *prepared_buffer,
    UINT32 imgW, UINT32 imgH,
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
        const UINT32 *src = prepared_buffer + y * drawW;
        UINT32 *dst = fb + (offY + y) * pitch + offX;
        CopyMem(dst, src, drawW * sizeof(UINT32));
    }
}

/* ─────────────────────────────────────────────
   PlayLogoAnimation – نسخة تدور 3 مرات بسرعة متوسطة
───────────────────────────────────────────── */
EFI_STATUS PlayLogoAnimation(
    EFI_FILE_HANDLE Root,
    CHAR16 *FolderName,
    UINT32  FrameCount,
    UINT32  FrameDelay,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    EFI_STATUS Status = EFI_SUCCESS;
    
    if (!Root || !FolderName || !gop || FrameCount == 0) {
        return EFI_INVALID_PARAMETER;
    }
    
    /* استخدام عدد الإطارات الأصلي */
    UINT32 actualFrameCount = FrameCount;
    if (actualFrameCount > 30) actualFrameCount = 30;
    
    /* هيكل للإطار مع مخزنه المحضر */
    typedef struct {
        UINT8  *rgba;
        UINT32 *prepared;
        UINT32  w, h;
    } FrameEntry;
    
    FrameEntry *frameTable = AllocateZeroPool(actualFrameCount * sizeof(FrameEntry));
    if (!frameTable) {
        return EFI_OUT_OF_RESOURCES;
    }
    
    CHAR16 path[512];
    UINT32 loadedCount = 0;
    
    /* تحميل جميع الإطارات أولاً */
    for (UINT32 i = 0; i < actualFrameCount && loadedCount < actualFrameCount; i++) {
        build_path(path, FolderName, i + 1);
        
        UINT32 fw = 0, fh = 0;
        UINT8 *rgba = load_png_frame(Root, path, &fw, &fh);
        if (!rgba) {
            continue;
        }
        
        frameTable[loadedCount].rgba = rgba;
        frameTable[loadedCount].w = fw;
        frameTable[loadedCount].h = fh;
        
        frameTable[loadedCount].prepared = prepare_frame_buffer(rgba, fw, fh, gop);
        if (!frameTable[loadedCount].prepared) {
            FreePool(rgba);
            continue;
        }
        
        loadedCount++;
    }
    
    if (loadedCount == 0) {
        FreePool(frameTable);
        return EFI_NOT_FOUND;
    }
    
    /* ============================================================
       التشغيل: 3 مرات مع تأخير مناسب بين كل دورة
       ============================================================ */
    
    for (UINT32 loop = 0; loop < ANIMATION_LOOP_COUNT; loop++) {
        
        /* مسح الشاشة قبل كل دورة (لأول مرة فقط اختياري) */
        if (loop == 0) {
            clear_screen(gop);
        }
        
        /* عرض جميع الإطارات بالتسلسل */
        for (UINT32 i = 0; i < loadedCount; i++) {
            if (!frameTable[i].prepared) continue;
            
            /* رسم الإطار */
            render_frame_double_buffer(frameTable[i].prepared, 
                                       frameTable[i].w, 
                                       frameTable[i].h, 
                                       gop);
            
            /* تأخير متوسط (السرعة المطلوبة) */
            /* FrameDelay = 40000 مثلاً يعطي 25 إطار بالثانية ~ 40ms للإطار */
            stall_us(FrameDelay);
        }
        
        /* تأخير بسيط بين الدورات (اختياري) */
        if (loop < ANIMATION_LOOP_COUNT - 1) {
            stall_us(50000); /* 50ms بين كل دورة */
        }
    }
    
    /* تنظيف الشاشة بعد الأنيميشن */
    clear_screen(gop);
    
    /* تنظيف الذاكرة */
    for (UINT32 i = 0; i < loadedCount; i++) {
        if (frameTable[i].prepared) FreePool(frameTable[i].prepared);
        if (frameTable[i].rgba) FreePool(frameTable[i].rgba);
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
