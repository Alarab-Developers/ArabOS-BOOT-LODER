#ifndef MENU_H
#define MENU_H

#include <efi.h>
#include <efilib.h>
#include "محرك_الاقلاع/محرك_الاقلاع.h"

/* ─────────────────────────────────────────────
   محرك_القائمة  –  قائمة إقلاع رسومية عربية
   ─────────────────────────────────────────────
   تعتمد كلياً على الصور PNG:
     • نافذه.png      - خلفية النافذة (مقاس 1000×1000)
     • شريط.png       - شريط العنوان
     • تحديد.png      - خلفية العنصر المحدد
     • سهم.png        - مؤشر السهم (حد أقصى 256×256)
     • شريط_المهلة.png - شريط العد التنازلي
─────────────────────────────────────────────── */

#define MENU_MAX_ENTRIES   16
#define MENU_DEFAULT_TIMEOUT  10

/* المقاس الثابت للقائمة */
#define MENU_FIXED_WIDTH    1000
#define MENU_FIXED_HEIGHT   500

/* الحد الأقصى لأبعاد صورة السهم */
#define MAX_ARROW_WIDTH     300
#define MAX_ARROW_HEIGHT    300

/* أبعاد العناصر (بالبكسل) */
#define MENU_ROW_HEIGHT      44
#define MENU_TITLE_H         46
#define MENU_HINT_H          30
#define MENU_TIMEOUT_BAR_H   6
#define MENU_PADDING_X       20
#define MENU_PADDING_Y       10
#define MENU_BORDER_W        2

#define SCANCODE_ESC    0x17
#define MENU_MAX_BUTTONS 16

/* ثوابت الأزرار */
#define MENU_BUTTON_H        44          /* ارتفاع الزر */
#define MENU_BUTTON_SPACING  10          /* المسافة بين الأزرار */

/* ثوابت التلاشي المحسنة - تلاشي سلس وطبيعي */
#define FADE_STEPS 12          /* عدد خطوات التلاشي */
#define FADE_DELAY 16667       /* 16.67ms = 60fps - تلاشي سلس جداً */

typedef struct {
    CHAR8   Label[64];     /* اسم العنصر بـ UTF-8 */
    CHAR16  Path[256];     /* مسار EFI */
    CHAR16  Args[256];     /* وسائط اختيارية */
} MenuEntry;

typedef struct {
    MenuEntry Entries[MENU_MAX_ENTRIES];
    UINT32    Count;
    UINT32    Selected;
    UINT32    TimeoutSec;
    CHAR8     Title[64];
    
    /* صور القائمة */
    UINT8     *BgImage;      /* خلفية النافذة (نافذه.png) - مقاس 1000×1000 */
    UINT32    BgW, BgH;
    
    UINT8     *TitleBarImage; /* شريط العنوان (شريط.png) */
    UINT32    TitleBarW, TitleBarH;
    
    UINT8     *SelectedImage; /* خلفية العنصر المحدد (تحديد.png) */
    UINT32    SelectedW, SelectedH;
    
    UINT8     *ArrowImage;    /* سهم المؤشر (سهم.png) - محجم إلى 256×256 كحد أقصى */
    UINT32    ArrowW, ArrowH;
    
    UINT8     *ProgressImage; /* شريط المهلة (شريط_المهلة.png) */
    UINT32    ProgressW, ProgressH;
    
    /* خلفية الشاشة الكاملة */
    UINT8     *ScreenBgImage;
    UINT32    ScreenBgW, ScreenBgH;
    CHAR16    ScreenBgPath[256];
    CHAR16 MenuBgPath[256];
    CHAR16 TitleBarPath[256];
    CHAR16 SelectedPath[256];
    CHAR16 ArrowPath[300];
    CHAR16 ProgressPath[256];

    CHAR16 ButtonPath[MENU_MAX_BUTTONS][256];
    UINT8  *ButtonImage[MENU_MAX_BUTTONS];
    UINT32  ButtonW[MENU_MAX_BUTTONS];
    UINT32  ButtonH[MENU_MAX_BUTTONS];

} BootMenu;

/* ── الدوال العامة ── */

VOID Menu_Init(BootMenu *menu, const CHAR8 *title, UINT32 timeoutSec);

BOOLEAN Menu_AddEntry(
    BootMenu      *menu,
    const CHAR8   *label,
    const CHAR16  *path,
    const CHAR16  *args);

VOID Menu_ReadFromFile(
    EFI_FILE_HANDLE Root,
    BootMenu       *menu,
    CHAR16         *outBackground,
    CHAR16         *outAnimDir);

VOID Menu_LoadImages(
    EFI_FILE_HANDLE Root,
    BootMenu       *menu,
    CHAR16         *MenuBgPath,
    CHAR16         *TitleBarPath,
    CHAR16         *SelectedPath,
    CHAR16         *ArrowPath,
    CHAR16         *ProgressPath);

VOID Menu_LoadScreenBackground(
    EFI_FILE_HANDLE Root,
    BootMenu       *menu,
    CHAR16         *BgPath);

VOID Menu_LoadButtonImages(
    EFI_FILE_HANDLE Root,
    BootMenu *menu);

VOID Menu_FreeImages(BootMenu *menu);

UINT32 Menu_Run(
    BootMenu                     *menu,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
    EFI_SYSTEM_TABLE             *SystemTable);

EFI_STATUS Menu_Boot(
    EFI_HANDLE       ImageHandle,
    EFI_SYSTEM_TABLE *SystemTable,
    MenuEntry        *entry);

#endif /* MENU_H */
