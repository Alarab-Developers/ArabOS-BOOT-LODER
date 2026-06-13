/*
 * محرك_القائمة.c
 * ──────────────
 * قائمة إقلاع رسومية عربية تعمل في بيئة UEFI فوق GOP.
 */

#include "محرك_القائمة.h"
#include "محرك_ادارة_الصور.h"
#include "محرك_التمهيد.h"

/* ═══════════════════════════════════════════════════════
   Structures
   ═══════════════════════════════════════════════════════ */

typedef struct {
    UINT32 *fb;
    UINT32 *backbuf;
    UINTN   scrW;
    UINTN   scrH;
    UINTN   pitch;
} Canvas;

typedef struct {
    UINTN x, y;
    UINTN w, h;
    UINTN titleH;
    UINTN rowH;
    UINTN hintH;
    UINTN bodyY;
    UINTN hintY;
} WinGeom;

/* ═══════════════════════════════════════════════════════
   Image scaling functions
   ═══════════════════════════════════════════════════════ */

static UINT8 *scale_image_rgba(const UINT8 *src, UINT32 srcW, UINT32 srcH,
                                UINT32 dstW, UINT32 dstH, UINT32 *outW, UINT32 *outH)
{
    if (!src || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0)
        return NULL;
    
    UINTN dstSize = (UINTN)dstW * dstH * 4;
    UINT8 *dst = AllocatePool(dstSize);
    if (!dst) return NULL;
    
    for (UINT32 y = 0; y < dstH; y++) {
        UINT32 srcY = (UINT32)(((UINT64)y * srcH) / dstH);
        UINT8 *dstRow = dst + (UINTN)y * dstW * 4;
        const UINT8 *srcRow = src + (UINTN)srcY * srcW * 4;
        
        for (UINT32 x = 0; x < dstW; x++) {
            UINT32 srcX = (UINT32)(((UINT64)x * srcW) / dstW);
            UINT32 srcIdx = srcX * 4;
            UINT32 dstIdx = x * 4;
            
            dstRow[dstIdx + 0] = srcRow[srcIdx + 0];
            dstRow[dstIdx + 1] = srcRow[srcIdx + 1];
            dstRow[dstIdx + 2] = srcRow[srcIdx + 2];
            dstRow[dstIdx + 3] = srcRow[srcIdx + 3];
        }
    }
    
    *outW = dstW;
    *outH = dstH;
    return dst;
}

static UINT8 *resize_to_fixed(const UINT8 *src, UINT32 srcW, UINT32 srcH,
                               UINT32 *outW, UINT32 *outH)
{
    return scale_image_rgba(src, srcW, srcH, 
                            MENU_FIXED_WIDTH, MENU_FIXED_HEIGHT,
                            outW, outH);
}

static UINT8 *resize_arrow_to_max(const UINT8 *src, UINT32 srcW, UINT32 srcH,
                                   UINT32 *outW, UINT32 *outH)
{
    UINT32 newW = srcW;
    UINT32 newH = srcH;
    BOOLEAN needsScale = FALSE;
    
    if (srcW > MAX_ARROW_WIDTH) {
        newW = MAX_ARROW_WIDTH;
        newH = (UINT32)(((UINT64)srcH * MAX_ARROW_WIDTH) / srcW);
        needsScale = TRUE;
    }
    
    if (newH > MAX_ARROW_HEIGHT) {
        newH = MAX_ARROW_HEIGHT;
        newW = (UINT32)(((UINT64)newW * MAX_ARROW_HEIGHT) / srcH);
        needsScale = TRUE;
    }
    
    if (needsScale && (newW != srcW || newH != srcH)) {
        return scale_image_rgba(src, srcW, srcH, newW, newH, outW, outH);
    }
    
    UINTN size = (UINTN)srcW * srcH * 4;
    UINT8 *copy = AllocatePool(size);
    if (copy) {
        CopyMem(copy, src, size);
        *outW = srcW;
        *outH = srcH;
    }
    return copy;
}

static UINT8 *resize_to_screen(const UINT8 *src, UINT32 srcW, UINT32 srcH,
                                UINTN scrW, UINTN scrH,
                                UINT32 *outW, UINT32 *outH)
{
    return scale_image_rgba(src, srcW, srcH, (UINT32)scrW, (UINT32)scrH, outW, outH);
}

/* ═══════════════════════════════════════════════════════
   Low-level drawing
   ═══════════════════════════════════════════════════════ */

static void canvas_init(Canvas *c, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    c->fb    = (UINT32 *)gop->Mode->FrameBufferBase;
    c->scrW  = gop->Mode->Info->HorizontalResolution;
    c->scrH  = gop->Mode->Info->VerticalResolution;
    c->pitch = gop->Mode->Info->PixelsPerScanLine;
    
    UINTN screenBytes = c->scrH * c->pitch * sizeof(UINT32);
    c->backbuf = AllocatePool(screenBytes);
    if (!c->backbuf) {
        c->backbuf = c->fb;
    }
}

static void canvas_free(Canvas *c)
{
    if (c->backbuf && c->backbuf != c->fb) {
        FreePool(c->backbuf);
    }
    c->backbuf = NULL;
    c->fb = NULL;
}

static void blit_image_scaled(Canvas *c, 
    const UINT8 *rgba, UINT32 imgW, UINT32 imgH,
    UINTN dstX, UINTN dstY, UINTN dstW, UINTN dstH)
{
    if (!rgba || imgW == 0 || imgH == 0) return;
    if (!c->backbuf) return;

    for (UINTN y = 0; y < dstH && (dstY + y) < c->scrH; y++) {
        UINT32 srcY = (UINT32)(((UINT64)y * imgH) / dstH);
        UINT32 *dst = c->backbuf + (dstY + y) * c->pitch + dstX;

        for (UINTN x = 0; x < dstW && (dstX + x) < c->scrW; x++) {
            UINT32 srcX = (UINT32)(((UINT64)x * imgW) / dstW);
            const UINT8 *p = rgba + ((UINTN)srcY * imgW + srcX) * 4;

            UINT8 R = p[0];
            UINT8 G = p[1];
            UINT8 B = p[2];
            UINT8 A = p[3];

            if (A == 255) {
                dst[x] = ((UINT32)R << 16) | ((UINT32)G << 8) | B;
            }
            else if (A > 0) {
                UINT32 bg = dst[x];
                UINT8 Br = (bg >> 16) & 0xFF;
                UINT8 Bg = (bg >> 8) & 0xFF;
                UINT8 Bb = bg & 0xFF;
                
                R = (UINT8)((R * A + Br * (255 - A)) >> 8);
                G = (UINT8)((G * A + Bg * (255 - A)) >> 8);
                B = (UINT8)((B * A + Bb * (255 - A)) >> 8);
                dst[x] = ((UINT32)R << 16) | ((UINT32)G << 8) | B;
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════
   Geometry calculation
   ═══════════════════════════════════════════════════════ */

static void calc_geom(Canvas *c, BootMenu *menu, UINT32 entryCount, WinGeom *g)
{
    g->w = MENU_FIXED_WIDTH;
    g->h = MENU_FIXED_HEIGHT;
    
    g->titleH = MENU_TITLE_H;
    g->rowH   = MENU_ROW_HEIGHT;
    g->hintH  = MENU_HINT_H + MENU_TIMEOUT_BAR_H + 4;
    
    UINTN availableHeight = g->h - g->titleH - g->hintH - (MENU_PADDING_Y * 3);
    UINTN maxEntries = availableHeight / g->rowH;
    
    if (entryCount > maxEntries && entryCount > 0) {
        g->rowH = availableHeight / entryCount;
        if (g->rowH < 30) g->rowH = 30;
    }
    
    g->x = (c->scrW > g->w) ? (c->scrW - g->w) / 2 : 0;
    g->y = (c->scrH > g->h) ? (c->scrH - g->h) / 2 : 0;
    
    g->bodyY = g->y + g->titleH + MENU_PADDING_Y;
    g->hintY = g->y + g->h - g->hintH - MENU_PADDING_Y;
}

/* ═══════════════════════════════════════════════════════
   Menu drawing functions
   ═══════════════════════════════════════════════════════ */

static void draw_screen_background(Canvas *c, BootMenu *menu)
{
    if (menu->ScreenBgImage && menu->ScreenBgW > 0 && menu->ScreenBgH > 0) {
        blit_image_scaled(c, menu->ScreenBgImage, 
            menu->ScreenBgW, menu->ScreenBgH,
            0, 0, c->scrW, c->scrH);
    } else {
        UINTN screenBytes = c->scrH * c->pitch * sizeof(UINT32);
        SetMem(c->backbuf, screenBytes, 0);
    }
}

static void draw_window_background(Canvas *c, BootMenu *menu, WinGeom *g)
{
    if (menu->BgImage && menu->BgW > 0 && menu->BgH > 0) {
        blit_image_scaled(c, menu->BgImage, menu->BgW, menu->BgH,
            g->x, g->y, g->w, g->h);
    }
}

static void draw_titlebar(Canvas *c, BootMenu *menu, WinGeom *g)
{
    if (menu->TitleBarImage && menu->TitleBarW > 0 && menu->TitleBarH > 0) {
        blit_image_scaled(c, menu->TitleBarImage, 
            menu->TitleBarW, menu->TitleBarH,
            g->x, g->y, g->w, g->titleH);
    }
}

/* رسم عنصر القائمة مع صورة زر إذا وجدت */
static void draw_menu_item(Canvas *c, BootMenu *menu, WinGeom *g,
    UINT32 idx, BOOLEAN selected)
{
    UINTN itemY = g->bodyY + idx * g->rowH;
    
    /* رسم خلفية التحديد */
    if (selected && menu->SelectedImage && menu->SelectedW > 0 && menu->SelectedH > 0) {
        blit_image_scaled(c, menu->SelectedImage,
            menu->SelectedW, menu->SelectedH,
            g->x + MENU_BORDER_W, itemY,
            g->w - MENU_BORDER_W * 2, g->rowH);
    }
    
    /* رسم صورة الزر إذا وجدت لهذا العنصر */
    UINT8 *buttonImg = NULL;
    UINT32 btnW = 0, btnH = 0;
    
    if (idx < MENU_MAX_BUTTONS && menu->ButtonImage[idx]) {
        buttonImg = menu->ButtonImage[idx];
        btnW = menu->ButtonW[idx];
        btnH = menu->ButtonH[idx];
    }
    
    if (buttonImg && btnW > 0 && btnH > 0) {
        UINT32 targetH = g->rowH - 4;
        UINT32 targetW = (UINT32)(((UINT64)btnW * targetH) / btnH);
        
        if (targetW > g->w - MENU_PADDING_X * 4) {
            targetW = g->w - MENU_PADDING_X * 4;
            targetH = (UINT32)(((UINT64)btnH * targetW) / btnW);
        }
        
        UINTN btnX = g->x + MENU_PADDING_X + (selected ? 25 : 10);
        
        blit_image_scaled(c, buttonImg, btnW, btnH,
                          btnX, itemY + (g->rowH - targetH) / 2,
                          targetW, targetH);
    } else if (menu->Entries[idx].Label[0]) {
        /* رسم النص كبديل (يمكنك تخصيصه) */
        /* Print(L"Menu item %d: %a\n", idx, menu->Entries[idx].Label); */
    }
    
    /* رسم السهم للعنصر المحدد */
    if (selected && menu->ArrowImage && menu->ArrowW > 0 && menu->ArrowH > 0) {
        UINT32 arrowSize = menu->ArrowW;
        if (arrowSize > g->rowH - 8) {
            arrowSize = (UINT32)(g->rowH - 8);
        }
        
        blit_image_scaled(c, menu->ArrowImage, menu->ArrowW, menu->ArrowH,
            g->x + MENU_PADDING_X - 5,
            itemY + (g->rowH - arrowSize) / 2,
            arrowSize, arrowSize);
    }
}

static void draw_progress_bar(Canvas *c, BootMenu *menu, WinGeom *g,
    UINT32 timeoutLeft, UINT32 timeoutTotal)
{
    if (!menu->ProgressImage || menu->ProgressW == 0 || menu->ProgressH == 0)
        return;
    
    if (timeoutTotal > 0 && timeoutLeft <= timeoutTotal)
    {
        UINTN barY = g->hintY + g->hintH - MENU_TIMEOUT_BAR_H - 4;
        UINTN barW = g->w - MENU_PADDING_X * 2;
        UINTN barX = g->x + MENU_PADDING_X;
        
        UINTN filledW = (barW * timeoutLeft) / timeoutTotal;
        
        if (filledW > 0) {
            blit_image_scaled(c, menu->ProgressImage,
                menu->ProgressW, menu->ProgressH,
                barX, barY, filledW, MENU_TIMEOUT_BAR_H);
        }
    }
}

static void redraw_menu(Canvas *c, BootMenu *menu, WinGeom *g,
    UINT32 timeoutLeft)
{
    if (!c->backbuf) return;
    
    draw_screen_background(c, menu);
    draw_window_background(c, menu, g);
    draw_titlebar(c, menu, g);
    
    for (UINT32 i = 0; i < menu->Count; i++) {
        draw_menu_item(c, menu, g, i, (i == menu->Selected));
    }
    
    draw_progress_bar(c, menu, g, timeoutLeft, menu->TimeoutSec);
    
    UINTN screenBytes = c->scrH * c->pitch * sizeof(UINT32);
    CopyMem(c->fb, c->backbuf, screenBytes);
}

/* ═══════════════════════════════════════════════════════
   Keyboard input
   ═══════════════════════════════════════════════════════ */

#define SCANCODE_UP     0x01
#define SCANCODE_DOWN   0x02
#define SCANCODE_ESC    0x17

typedef enum {
    KEY_UP, KEY_DOWN, KEY_ENTER, KEY_ESC, KEY_NONE
} MenuKey;

/* ═══════════════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════════════ */

VOID Menu_Init(BootMenu *menu, const CHAR8 *title, UINT32 timeoutSec)
{
    SetMem(menu, sizeof(BootMenu), 0);
    menu->TimeoutSec = timeoutSec;

    for (UINT32 i = 0; i < MENU_MAX_BUTTONS; i++) {
        menu->ButtonPath[i][0] = L'\0';
        menu->ButtonImage[i] = NULL;
        menu->ButtonW[i] = 0;
        menu->ButtonH[i] = 0;
    }

    if (title) {
        UINTN i = 0;
        while (title[i] && i < sizeof(menu->Title) - 1) {
            menu->Title[i] = title[i];
            i++;
        }
        menu->Title[i] = '\0';
    }
}

VOID Menu_FreeImages(BootMenu *menu)
{
    UINT32 i;

    if (menu->BgImage) { FreePool(menu->BgImage); menu->BgImage = NULL; }
    if (menu->TitleBarImage) { FreePool(menu->TitleBarImage); menu->TitleBarImage = NULL; }
    if (menu->SelectedImage) { FreePool(menu->SelectedImage); menu->SelectedImage = NULL; }
    if (menu->ArrowImage) { FreePool(menu->ArrowImage); menu->ArrowImage = NULL; }
    if (menu->ProgressImage) { FreePool(menu->ProgressImage); menu->ProgressImage = NULL; }
    if (menu->ScreenBgImage) { FreePool(menu->ScreenBgImage); menu->ScreenBgImage = NULL; }

    for (i = 0; i < MENU_MAX_BUTTONS; i++) {
        if (menu->ButtonImage[i]) {
            FreePool(menu->ButtonImage[i]);
            menu->ButtonImage[i] = NULL;
            menu->ButtonW[i] = 0;
            menu->ButtonH[i] = 0;
        }
    }
}

VOID Menu_LoadImages(
    EFI_FILE_HANDLE Root,
    BootMenu       *menu,
    CHAR16         *MenuBgPath,
    CHAR16         *TitleBarPath,
    CHAR16         *SelectedPath,
    CHAR16         *ArrowPath,
    CHAR16         *ProgressPath)
{
    UINT32 tempW, tempH;
    UINT8 *tempImage;
    
    Menu_FreeImages(menu);
    
    if (MenuBgPath && MenuBgPath[0]) {
        tempImage = load_png_frame(Root, MenuBgPath, &tempW, &tempH);
        if (tempImage) {
            menu->BgImage = resize_to_fixed(tempImage, tempW, tempH, &menu->BgW, &menu->BgH);
            FreePool(tempImage);
        }
    }
    
    if (TitleBarPath && TitleBarPath[0]) {
        menu->TitleBarImage = load_png_frame(Root, TitleBarPath, &menu->TitleBarW, &menu->TitleBarH);
    }
    
    if (SelectedPath && SelectedPath[0]) {
        menu->SelectedImage = load_png_frame(Root, SelectedPath, &menu->SelectedW, &menu->SelectedH);
    }
    
    if (ArrowPath && ArrowPath[0]) {
        tempImage = load_png_frame(Root, ArrowPath, &tempW, &tempH);
        if (tempImage) {
            menu->ArrowImage = resize_arrow_to_max(tempImage, tempW, tempH, &menu->ArrowW, &menu->ArrowH);
            FreePool(tempImage);
        }
    }
    
    if (ProgressPath && ProgressPath[0]) {
        menu->ProgressImage = load_png_frame(Root, ProgressPath, &menu->ProgressW, &menu->ProgressH);
    }
}

VOID Menu_LoadScreenBackground(
    EFI_FILE_HANDLE Root,
    BootMenu       *menu,
    CHAR16         *BgPath)
{
    UINT8 *tempImage;
    UINT32 tempW, tempH;
    
    if (menu->ScreenBgImage) {
        FreePool(menu->ScreenBgImage);
        menu->ScreenBgImage = NULL;
    }
    
    if (BgPath && BgPath[0]) {
        UINTN i = 0;
        while (BgPath[i] && i < 255) {
            menu->ScreenBgPath[i] = BgPath[i];
            i++;
        }
        menu->ScreenBgPath[i] = L'\0';
        
        tempImage = load_png_frame(Root, BgPath, &tempW, &tempH);
        if (tempImage) {
            menu->ScreenBgImage = tempImage;
            menu->ScreenBgW = tempW;
            menu->ScreenBgH = tempH;
        }
    }
}

BOOLEAN Menu_AddEntry(BootMenu *menu,
    const CHAR8 *label, const CHAR16 *path, const CHAR16 *args)
{
    if (menu->Count >= MENU_MAX_ENTRIES) return FALSE;

    MenuEntry *e = &menu->Entries[menu->Count];
    SetMem(e, sizeof(MenuEntry), 0);

    UINTN i = 0;
    while (label && label[i] && i < sizeof(e->Label) - 1) {
        e->Label[i] = label[i]; i++;
    }
    e->Label[i] = '\0';

    if (path) {
        UINTN j = 0;
        while (path[j] && j < 255) { e->Path[j] = path[j]; j++; }
        e->Path[j] = L'\0';
    }

    if (args) {
        UINTN k = 0;
        while (args[k] && k < 255) { e->Args[k] = args[k]; k++; }
        e->Args[k] = L'\0';
    }

    menu->Count++;
    return TRUE;
}
/* ═══════════════════════════════════════════════════════
   Read config from file - النسخة المصححة
   ═══════════════════════════════════════════════════════ */

static BOOLEAN starts_with(const CHAR8 *line, const CHAR8 *prefix)
{
    while (*prefix) {
        if (*line++ != *prefix++) return FALSE;
    }
    return TRUE;
}

static void trim_leading(CHAR8 **p)
{
    while (**p == ' ' || **p == '\t') (*p)++;
}

VOID Menu_ReadFromFile(
    EFI_FILE_HANDLE Root,
    BootMenu       *menu,
    CHAR16         *outBackground,
    CHAR16         *outAnimDir)
{
    static const CHAR8 BG_KEY[]       = "\xd8\xa7\xd9\x84\xd8\xae\xd9\x84\xd9\x81\xd9\x8a\xd8\xa9 =";
    static const CHAR8 LOGO_KEY[]     = "\xd8\xb4\xd8\xb9\xd8\xa7\xd8\xb1 \xd8\xa7\xd9\x84\xd8\xaa\xd9\x85\xd9\x87\xd9\x8a\xd8\xaf =";
    static const CHAR8 MENUBG_KEY[]   = "\xd8\xae\xd9\x84\xd9\x81\xd9\x8a\xd8\xa9 \xd8\xa7\xd9\x84\xd9\x82\xd8\xa7\xd8\xa6\xd9\x85\xd8\xa9 =";
    static const CHAR8 TITLEBAR_KEY[] = "\xd8\xb4\xd8\xb1\xd9\x8a\xd8\xb7 \xd8\xa7\xd9\x84\xd8\xb9\xd9\x86\xd9\x88\xd8\xa7\xd9\x86 =";
    static const CHAR8 SELECTED_KEY[] = "\xd8\xa7\xd9\x84\xd8\xaa\xd8\xad\xd8\xaf\xd9\x8a\xd8\xaf =";
    static const CHAR8 ARROW_KEY[]    = "\xd8\xa7\xd9\x84\xd8\xb3\xd9\x87\xd9\x85 =";
    static const CHAR8 PROGRESS_KEY[] = "\xd8\xb4\xd8\xb1\xd9\x8a\xd8\xb7 \xd8\xa7\xd9\x84\xd9\x85\xd9\x87\xd9\x84\xd8\xa9 =";
    static const CHAR8 TITLE_KEY[]    = "\xd8\xa7\xd9\x84\xd8\xb9\xd9\x86\xd9\x88\xd8\xa7\xd9\x86 =";
    static const CHAR8 TIMEOUT_KEY[]  = "\xd9\x85\xd9\x87\xd9\x84\xd8\xa9 =";
    static const CHAR8 ENTRY_KEY[]    = "\xd9\x86\xd8\xb8\xd8\xa7\xd9\x85 =";
    static const CHAR8 BUTTON_KEY[]   = "\xd8\xb2\xd8\xb1 ";

    EFI_FILE_HANDLE File;
    EFI_STATUS st;

    /* ========================================================
       التصحيح: المسار الصحيح لمجلد اعدادات_القائمة داخل المقلع
       ======================================================== */
    CHAR16 cfgPath[256];
    ArabicPath(cfgPath, 256, "المقلع\\اعدادات_القائمة\\القائمة.نص");

    /* طباعة المسار للتصحيح (اختياري) */
    Print(L"Loading config from: %s\n", cfgPath);

    st = uefi_call_wrapper(Root->Open, 5, Root, &File, cfgPath, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st)) {
        Print(L"Error: Cannot open config file. Status = %r\n", st);
        return;
    }

    CHAR8 buf[8192];
    UINTN sz = sizeof(buf) - 1;
    st = uefi_call_wrapper(File->Read, 3, File, &sz, buf);
    uefi_call_wrapper(File->Close, 1, File);
    if (EFI_ERROR(st)) {
        Print(L"Error: Cannot read config file. Status = %r\n", st);
        return;
    }

    buf[sz] = '\0';
    UINTN pos = 0;

    /* إزالة BOM إذا وجد */
    if (sz >= 3 && (UINT8)buf[0] == 0xEF && (UINT8)buf[1] == 0xBB && (UINT8)buf[2] == 0xBF) {
        pos = 3;
    }

    while (buf[pos]) {
        CHAR8 *line = buf + pos;
        while (buf[pos] && buf[pos] != '\r' && buf[pos] != '\n') pos++;
        CHAR8 saved = buf[pos];
        buf[pos] = '\0';

        if (line[0] != '#' && line[0] != ';' && line[0] != '[' && line[0] != '\0') {
            if (starts_with(line, BG_KEY)) {
                CHAR8 *val = line + sizeof(BG_KEY) - 1;
                trim_leading(&val);
                Utf8ToUcs2(outBackground, 256, val);
                Print(L"Background path: %s\n", outBackground);
            }
            else if (starts_with(line, LOGO_KEY)) {
                CHAR8 *val = line + sizeof(LOGO_KEY) - 1;
                trim_leading(&val);
                Utf8ToUcs2(outAnimDir, 256, val);
                Print(L"Animation dir: %s\n", outAnimDir);
            }
            else if (starts_with(line, MENUBG_KEY)) {
                CHAR8 *val = line + sizeof(MENUBG_KEY) - 1;
                trim_leading(&val);
                Utf8ToUcs2(menu->MenuBgPath, 256, val);
            }
            else if (starts_with(line, TITLEBAR_KEY)) {
                CHAR8 *val = line + sizeof(TITLEBAR_KEY) - 1;
                trim_leading(&val);
                Utf8ToUcs2(menu->TitleBarPath, 256, val);
            }
            else if (starts_with(line, SELECTED_KEY)) {
                CHAR8 *val = line + sizeof(SELECTED_KEY) - 1;
                trim_leading(&val);
                Utf8ToUcs2(menu->SelectedPath, 256, val);
            }
            else if (starts_with(line, ARROW_KEY)) {
                CHAR8 *val = line + sizeof(ARROW_KEY) - 1;
                trim_leading(&val);
                Utf8ToUcs2(menu->ArrowPath, 256, val);
            }
            else if (starts_with(line, PROGRESS_KEY)) {
                CHAR8 *val = line + sizeof(PROGRESS_KEY) - 1;
                trim_leading(&val);
                Utf8ToUcs2(menu->ProgressPath, 256, val);
            }
            else if (starts_with(line, BUTTON_KEY)) {
                CHAR8 *p = line + sizeof(BUTTON_KEY) - 1;
                UINT32 index = 0;
                while (*p >= '0' && *p <= '9') {
                    index = (index * 10) + (*p - '0');
                    p++;
                }
                if (index >= 1 && index <= MENU_MAX_BUTTONS) {
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == '=') {
                        p++;
                        trim_leading(&p);
                        Utf8ToUcs2(menu->ButtonPath[index - 1], 256, p);
                        
                        /* إضافة عنصر قائمة للزر */
                        CHAR8 labelBuf[64] = "خيار ";
                        CHAR8 numStr[4];
                        numStr[0] = (CHAR8)('0' + index);
                        numStr[1] = '\0';
                        UINTN len = 6; /* طول "خيار " */
                        for (UINTN j = 0; j < 2 && numStr[j]; j++) {
                            labelBuf[len + j] = numStr[j];
                        }
                        labelBuf[len + 2] = '\0';
                        
                        CHAR16 defaultPath[256];
                        ArabicPath(defaultPath, 256, "\\EFI\\Boot\\bootx64.efi");
                        Menu_AddEntry(menu, labelBuf, defaultPath, NULL);
                    }
                }
            }
            else if (starts_with(line, TITLE_KEY)) {
                CHAR8 *val = line + sizeof(TITLE_KEY) - 1;
                trim_leading(&val);
                UINTN i = 0;
                while (val[i] && i < sizeof(menu->Title) - 1) {
                    menu->Title[i] = val[i];
                    i++;
                }
                menu->Title[i] = '\0';
            }
            else if (starts_with(line, TIMEOUT_KEY)) {
                CHAR8 *val = line + sizeof(TIMEOUT_KEY) - 1;
                trim_leading(&val);
                UINT32 n = 0;
                while (*val >= '0' && *val <= '9') {
                    n = (n * 10) + (*val - '0');
                    val++;
                }
                if (n > 0) menu->TimeoutSec = n;
            }
            else if (starts_with(line, ENTRY_KEY)) {
                CHAR8 *val = line + sizeof(ENTRY_KEY) - 1;
                trim_leading(&val);
                CHAR8 *sep = val;
                while (*sep && *sep != '|') sep++;
                CHAR8  labelBuf[64] = {0};
                CHAR16 pathBuf[256] = {0};
                CHAR16 argsBuf[256] = {0};
                UINTN llen = (UINTN)(sep - val);
                if (llen >= sizeof(labelBuf)) llen = sizeof(labelBuf) - 1;
                CopyMem(labelBuf, val, llen);
                labelBuf[llen] = '\0';
                if (*sep == '|') {
                    CHAR8 *pathUtf8 = sep + 1;
                    trim_leading(&pathUtf8);
                    CHAR8 *sep2 = pathUtf8;
                    while (*sep2 && *sep2 != '|') sep2++;
                    if (*sep2 == '|') {
                        CHAR8 pathTemp[256] = {0};
                        UINTN plen = (UINTN)(sep2 - pathUtf8);
                        if (plen >= sizeof(pathTemp)) plen = sizeof(pathTemp) - 1;
                        CopyMem(pathTemp, pathUtf8, plen);
                        pathTemp[plen] = '\0';
                        Utf8ToUcs2(pathBuf, 256, pathTemp);
                        CHAR8 *argsUtf8 = sep2 + 1;
                        trim_leading(&argsUtf8);
                        Utf8ToUcs2(argsBuf, 256, argsUtf8);
                    }
                    else {
                        Utf8ToUcs2(pathBuf, 256, pathUtf8);
                    }
                }
                Menu_AddEntry(menu, labelBuf, pathBuf[0] ? pathBuf : NULL, argsBuf[0] ? argsBuf : NULL);
            }
        }
        buf[pos] = saved;
        while (buf[pos] == '\r' || buf[pos] == '\n') pos++;
    }
    
    Print(L"Config loaded: %d entries found, Timeout=%d seconds\n", 
          menu->Count, menu->TimeoutSec);
}

/* ═══════════════════════════════════════════════════════
   Menu_Run
   ═══════════════════════════════════════════════════════ */

UINT32 Menu_Run(
    BootMenu                     *menu,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
    EFI_SYSTEM_TABLE             *SystemTable)
{
    if (menu->Count == 0) return MENU_MAX_ENTRIES;

    Canvas c;
    canvas_init(&c, gop);
    
    if (menu->ScreenBgImage && menu->ScreenBgW > 0 && menu->ScreenBgH > 0) {
        UINT32 newW, newH;
        UINT8 *scaled = resize_to_screen(menu->ScreenBgImage, 
            menu->ScreenBgW, menu->ScreenBgH,
            c.scrW, c.scrH, &newW, &newH);
        if (scaled) {
            FreePool(menu->ScreenBgImage);
            menu->ScreenBgImage = scaled;
            menu->ScreenBgW = newW;
            menu->ScreenBgH = newH;
        }
    }

    WinGeom g;
    calc_geom(&c, menu, menu->Count, &g);

    redraw_menu(&c, menu, &g, menu->TimeoutSec);

    UINT32 timeoutLeft = menu->TimeoutSec;
    UINT32 ticksLeft = (menu->TimeoutSec > 0) ? (menu->TimeoutSec * 10) : 0xFFFFFFFF;

    for (;;) {
        uefi_call_wrapper(BS->Stall, 1, 100000);

        EFI_INPUT_KEY key;
        EFI_STATUS st = uefi_call_wrapper(SystemTable->ConIn->ReadKeyStroke, 2, SystemTable->ConIn, &key);

        if (!EFI_ERROR(st)) {
            MenuKey mk = KEY_NONE;
            if (key.ScanCode == SCANCODE_UP)   mk = KEY_UP;
            if (key.ScanCode == SCANCODE_DOWN) mk = KEY_DOWN;
            if (key.ScanCode == SCANCODE_ESC)  mk = KEY_ESC;
            if (key.UnicodeChar == L'\r')      mk = KEY_ENTER;
            if (key.UnicodeChar == L'\n')      mk = KEY_ENTER;

            if (mk == KEY_UP) {
                if (menu->Selected > 0) menu->Selected--;
                else menu->Selected = menu->Count - 1;
                ticksLeft = 0xFFFFFFFF;
                timeoutLeft = 0;
                redraw_menu(&c, menu, &g, 0);
            }
            else if (mk == KEY_DOWN) {
                menu->Selected = (menu->Selected + 1) % menu->Count;
                ticksLeft = 0xFFFFFFFF;
                timeoutLeft = 0;
                redraw_menu(&c, menu, &g, 0);
            }
            else if (mk == KEY_ENTER) {
                canvas_free(&c);
                return menu->Selected;
            }
            else if (mk == KEY_ESC) {
                canvas_free(&c);
                return MENU_MAX_ENTRIES;
            }
        }

        if (ticksLeft != 0xFFFFFFFF) {
            if (ticksLeft == 0) {
                canvas_free(&c);
                return menu->Selected;
            }
            ticksLeft--;
            UINT32 newLeft = ticksLeft / 10;
            if (newLeft != timeoutLeft) {
                timeoutLeft = newLeft;
                redraw_menu(&c, menu, &g, timeoutLeft);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════
   Menu_Boot
   ═══════════════════════════════════════════════════════ */

EFI_STATUS Menu_Boot(
    EFI_HANDLE        ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable,
    MenuEntry        *entry)
{
    if (!entry || entry->Path[0] == L'\0')
        return EFI_NOT_FOUND;

    EFI_LOADED_IMAGE *li;
    EFI_GUID LIP = LOADED_IMAGE_PROTOCOL;
    EFI_STATUS st;

    st = uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &LIP, (void **)&li);
    if (EFI_ERROR(st)) return st;

    EFI_DEVICE_PATH *fileDp = FileDevicePath(li->DeviceHandle, entry->Path);
    if (!fileDp) return EFI_NOT_FOUND;

    EFI_HANDLE newImage;
    st = uefi_call_wrapper(BS->LoadImage, 6, FALSE, ImageHandle, fileDp, NULL, 0, &newImage);
    FreePool(fileDp);
    if (EFI_ERROR(st)) return st;

    if (entry->Args[0]) {
        EFI_LOADED_IMAGE *newLi;
        st = uefi_call_wrapper(BS->HandleProtocol, 3, newImage, &LIP, (void **)&newLi);
        if (!EFI_ERROR(st)) {
            newLi->LoadOptions = entry->Args;
            newLi->LoadOptionsSize = (UINT32)((StrLen(entry->Args) + 1) * sizeof(CHAR16));
        }
    }

    st = uefi_call_wrapper(BS->StartImage, 3, newImage, NULL, NULL);
    return st;
}

VOID Menu_LoadButtonImages(
    EFI_FILE_HANDLE Root,
    BootMenu *menu)
{
    UINT32 i;
    for (i = 0; i < MENU_MAX_BUTTONS; i++) {
        if (!menu->ButtonPath[i][0]) continue;
        menu->ButtonImage[i] = load_png_frame(Root, menu->ButtonPath[i], &menu->ButtonW[i], &menu->ButtonH[i]);
        if (menu->ButtonImage[i]) {
            Print(L"Button %d loaded (%d x %d)\n", i + 1, menu->ButtonW[i], menu->ButtonH[i]);
        }
    }
}
