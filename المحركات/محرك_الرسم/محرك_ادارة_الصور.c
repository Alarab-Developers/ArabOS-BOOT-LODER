#include "محرك_ادارة_الصور.h"

/* ─────────────────────────────────────────────
   BitReader Functions
───────────────────────────────────────────── */
static void br_init(BitReader *b, const UINT8 *src, UINTN len)
{
    b->src      = src;
    b->srcLen   = len;
    b->srcPos   = 0;
    b->bits     = 0;
    b->bitCount = 0;
}

static UINT32 br_bit(BitReader *b)
{
    if (b->bitCount == 0) {
        if (b->srcPos < b->srcLen)
            b->bits = b->src[b->srcPos++];
        else
            b->bits = 0;
        b->bitCount = 8;
    }
    UINT32 v = b->bits & 1;
    b->bits >>= 1;
    b->bitCount--;
    return v;
}

static UINT32 br_bits(BitReader *b, UINT32 n)
{
    UINT32 v = 0;
    for (UINT32 i = 0; i < n; i++)
        v |= br_bit(b) << i;
    return v;
}

static void br_align(BitReader *b)
{
    b->bits     = 0;
    b->bitCount = 0;
}

static UINT16 br_u16le(BitReader *b)
{
    UINT8 lo = (b->srcPos < b->srcLen) ? b->src[b->srcPos++] : 0;
    UINT8 hi = (b->srcPos < b->srcLen) ? b->src[b->srcPos++] : 0;
    return (UINT16)(lo | ((UINT16)hi << 8));
}

/* ── Huffman decoder ── */
#define MAX_CODES 288
#define MAX_BITS   16

typedef struct {
    UINT16 counts[MAX_BITS + 1];
    UINT16 symbols[MAX_CODES];
    INT32  maxBit;
} Huffman;

static BOOLEAN huff_build(Huffman *h, const UINT8 *lengths, INT32 n)
{
    INT32 offsets[MAX_BITS + 1];
    SetMem(h->counts, sizeof(h->counts), 0);
    SetMem(h->symbols, sizeof(h->symbols), 0);
    h->maxBit = 0;

    for (INT32 i = 0; i < n; i++) {
        if (lengths[i]) {
            h->counts[lengths[i]]++;
            if (lengths[i] > h->maxBit)
                h->maxBit = lengths[i];
        }
    }

    offsets[1] = 0;
    for (INT32 i = 1; i < MAX_BITS; i++)
        offsets[i + 1] = offsets[i] + h->counts[i];

    for (INT32 i = 0; i < n; i++)
        if (lengths[i])
            h->symbols[offsets[lengths[i]]++] = (UINT16)i;

    return TRUE;
}

static INT32 huff_decode(BitReader *b, const Huffman *h)
{
    INT32  code = 0;
    INT32  first = 0;
    INT32  index = 0;

    for (INT32 len = 1; len <= h->maxBit; len++) {
        code  = (code << 1) | (INT32)br_bit(b);
        INT32 count = (INT32)h->counts[len];
        if (code - count < first)
            return (INT32)h->symbols[index + (code - first)];
        index += count;
        first  = (first + count) << 1;
    }
    return -1; /* error */
}

/* ── DEFLATE length / distance tables ── */
static const UINT16 LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const UINT8 LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
    3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const UINT16 DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,
    8193,12289,16385,24577
};
static const UINT8 DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* output buffer */
typedef struct {
    UINT8 *buf;
    UINTN  cap;
    UINTN  pos;
} OutBuf;

static BOOLEAN ob_put(OutBuf *o, UINT8 c)
{
    if (o->pos >= o->cap) return FALSE;
    o->buf[o->pos++] = c;
    return TRUE;
}

static BOOLEAN ob_copy(OutBuf *o, UINT32 dist, UINT32 len)
{
    if (dist > o->pos) return FALSE;
    if (o->pos + len > o->cap) return FALSE;
    for (UINT32 i = 0; i < len; i++)
        o->buf[o->pos + i] = o->buf[o->pos - dist + i];
    o->pos += len;
    return TRUE;
}

/* inflate one DEFLATE stream into o->buf */
static BOOLEAN inflate_stream(OutBuf *o, const UINT8 *src, UINTN srcLen)
{
    BitReader br;
    br_init(&br, src, srcLen);

    for (;;) {
        UINT32 bfinal = br_bits(&br, 1);
        UINT32 btype  = br_bits(&br, 2);

        if (btype == 0) {
            /* ── uncompressed block ── */
            br_align(&br);
            UINT16 len  = br_u16le(&br);
            UINT16 nlen = br_u16le(&br);
            (void)nlen;
            for (UINT16 i = 0; i < len; i++) {
                if (br.srcPos >= br.srcLen) return FALSE;
                if (!ob_put(o, br.src[br.srcPos++])) return FALSE;
            }
        } else if (btype == 1 || btype == 2) {
            Huffman litH, distH;

            if (btype == 1) {
                /* ── fixed Huffman ── */
                UINT8 lens[288];
                for (INT32 i = 0;   i < 144; i++) lens[i] = 8;
                for (INT32 i = 144; i < 256; i++) lens[i] = 9;
                for (INT32 i = 256; i < 280; i++) lens[i] = 7;
                for (INT32 i = 280; i < 288; i++) lens[i] = 8;
                huff_build(&litH, lens, 288);

                UINT8 dlens[30];
                for (INT32 i = 0; i < 30; i++) dlens[i] = 5;
                huff_build(&distH, dlens, 30);
            } else {
                /* ── dynamic Huffman ── */
                UINT32 hlit  = br_bits(&br, 5) + 257;
                UINT32 hdist = br_bits(&br, 5) + 1;
                UINT32 hclen = br_bits(&br, 4) + 4;

                static const UINT8 CLEN_ORDER[19] = {
                    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
                };
                UINT8 clenLens[19] = {0};
                for (UINT32 i = 0; i < hclen; i++)
                    clenLens[CLEN_ORDER[i]] = (UINT8)br_bits(&br, 3);

                Huffman clenH;
                huff_build(&clenH, clenLens, 19);

                UINT8 allLens[288 + 32];
                UINT32 total = hlit + hdist;
                for (UINT32 i = 0; i < total; ) {
                    INT32 sym = huff_decode(&br, &clenH);
                    if (sym < 0) return FALSE;
                    if (sym < 16) {
                        allLens[i++] = (UINT8)sym;
                    } else if (sym == 16) {
                        UINT32 rep = br_bits(&br, 2) + 3;
                        UINT8  v   = (i > 0) ? allLens[i - 1] : 0;
                        for (UINT32 k = 0; k < rep && i < total; k++, i++)
                            allLens[i] = v;
                    } else if (sym == 17) {
                        UINT32 rep = br_bits(&br, 3) + 3;
                        for (UINT32 k = 0; k < rep && i < total; k++, i++)
                            allLens[i] = 0;
                    } else { /* 18 */
                        UINT32 rep = br_bits(&br, 7) + 11;
                        for (UINT32 k = 0; k < rep && i < total; k++, i++)
                            allLens[i] = 0;
                    }
                }
                huff_build(&litH,  allLens,        (INT32)hlit);
                huff_build(&distH, allLens + hlit, (INT32)hdist);
            }

            /* ── decode symbols ── */
            for (;;) {
                INT32 sym = huff_decode(&br, &litH);
                if (sym < 0)   return FALSE;
                if (sym == 256) break;
                if (sym < 256) {
                    if (!ob_put(o, (UINT8)sym)) return FALSE;
                } else {
                    UINT32 lenIdx  = (UINT32)(sym - 257);
                    if (lenIdx >= 29) return FALSE;
                    UINT32 len  = LEN_BASE[lenIdx]  + br_bits(&br, LEN_EXTRA[lenIdx]);
                    INT32  dsym = huff_decode(&br, &distH);
                    if (dsym < 0 || dsym >= 30) return FALSE;
                    UINT32 dist = DIST_BASE[dsym] + br_bits(&br, DIST_EXTRA[dsym]);
                    if (!ob_copy(o, dist, len)) return FALSE;
                }
            }
        } else {
            return FALSE; /* btype == 3: reserved, error */
        }

        if (bfinal) break;
    }
    return TRUE;
}

/* ─────────────────────────────────────────────
   PNG support
───────────────────────────────────────────── */
static UINT32 png_u32be(const UINT8 *p)
{
    return ((UINT32)p[0] << 24) |
           ((UINT32)p[1] << 16) |
           ((UINT32)p[2] <<  8) |
            (UINT32)p[3];
}

static BOOLEAN png_sig_ok(const UINT8 *d)
{
    static const UINT8 SIG[8] = {137,80,78,71,13,10,26,10};
    for (INT32 i = 0; i < 8; i++)
        if (d[i] != SIG[i]) return FALSE;
    return TRUE;
}

/* PNG filter reconstruction (in-place per row) */
static void png_unfilter(UINT8 *raw, UINT32 w, UINT32 bpp, UINT32 h)
{
    UINT32 stride = w * bpp + 1; /* +1 for filter byte */
    for (UINT32 y = 0; y < h; y++) {
        UINT8  filt  = raw[y * stride];
        UINT8 *row   = raw + y * stride + 1;
        UINT8 *prev  = (y > 0) ? raw + (y - 1) * stride + 1 : NULL;

        if (filt == 0) {
            /* None */
        } else if (filt == 1) {
            /* Sub */
            for (UINT32 x = bpp; x < w * bpp; x++)
                row[x] += row[x - bpp];
        } else if (filt == 2) {
            /* Up */
            if (prev)
                for (UINT32 x = 0; x < w * bpp; x++)
                    row[x] += prev[x];
        } else if (filt == 3) {
            /* Average */
            for (UINT32 x = 0; x < w * bpp; x++) {
                UINT8 a = (x >= bpp) ? row[x - bpp] : 0;
                UINT8 b = prev ? prev[x] : 0;
                row[x] += (UINT8)(((UINT16)a + b) >> 1);
            }
        } else if (filt == 4) {
            /* Paeth */
            for (UINT32 x = 0; x < w * bpp; x++) {
                UINT8 a  = (x >= bpp) ? row[x - bpp]        : 0;
                UINT8 b  = prev       ? prev[x]              : 0;
                UINT8 c  = (prev && x >= bpp) ? prev[x-bpp] : 0;
                INT32 p  = (INT32)a + b - c;
                INT32 pa = p - a; if (pa < 0) pa = -pa;
                INT32 pb = p - b; if (pb < 0) pb = -pb;
                INT32 pc = p - c; if (pc < 0) pc = -pc;
                UINT8 pr = (pa <= pb && pa <= pc) ? a
                         : (pb <= pc)             ? b
                                                  : c;
                row[x] += pr;
            }
        }
    }
}

/*
 * Decode a PNG file already loaded into memory.
 * Outputs: *outWidth, *outHeight, and an RGBA buffer
 * (4 bytes per pixel) allocated with AllocatePool.
 * Caller must FreePool() it.
 * Returns NULL on failure.
 */
static UINT8 *png_decode(
    const UINT8 *data, UINTN dataLen,
    UINT32 *outWidth, UINT32 *outHeight)
{
    if (dataLen < 8 || !png_sig_ok(data)) return NULL;

    UINT32 imgW = 0, imgH = 0, bitDepth = 0, colorType = 0;
    BOOLEAN ihdrFound = FALSE;

    /* collect all IDAT chunks into one buffer */
    UINTN idatCap = dataLen;          /* upper bound */
    UINT8 *idatBuf = AllocatePool(idatCap);
    if (!idatBuf) return NULL;
    UINTN idatLen = 0;

    UINTN pos = 8;
    while (pos + 12 <= dataLen) {
        UINT32 clen  = png_u32be(data + pos);      pos += 4;
        UINT32 ctype = png_u32be(data + pos);      pos += 4;
        const UINT8 *cdata = data + pos;
        if (pos + clen > dataLen) break;

        if (ctype == 0x49484452 /* IHDR */) {
            if (clen < 13) break;
            imgW      = png_u32be(cdata);
            imgH      = png_u32be(cdata + 4);
            bitDepth  = cdata[8];
            colorType = cdata[9];
            ihdrFound = TRUE;
        } else if (ctype == 0x49444154 /* IDAT */) {
            if (idatLen + clen > idatCap) {
                FreePool(idatBuf);
                return NULL;
            }
            CopyMem(idatBuf + idatLen, cdata, clen);
            idatLen += clen;
        } else if (ctype == 0x49454E44 /* IEND */) {
            break;
        }

        pos += clen + 4; /* skip CRC */
    }

    if (!ihdrFound || imgW == 0 || imgH == 0 || idatLen < 2) {
        FreePool(idatBuf);
        return NULL;
    }

    /* Only support 8-bit RGB and RGBA */
    if (bitDepth != 8 ||
        (colorType != 2 /* RGB */ && colorType != 6 /* RGBA */)) {

        FreePool(idatBuf);
        return NULL;
    }

    UINT32 srcBpp = (colorType == 6) ? 4 : 3;
    UINT32 stride = imgW * srcBpp + 1; /* +1 filter byte */

    /* allocate raw (filtered) output */
    UINTN rawSize = (UINTN)stride * imgH;
    UINT8 *rawBuf = AllocatePool(rawSize);
    if (!rawBuf) {
        FreePool(idatBuf);
        return NULL;
    }

    /* inflate: skip 2-byte zlib header */
    OutBuf ob;
    ob.buf = rawBuf;
    ob.cap = rawSize;
    ob.pos = 0;

    if (!inflate_stream(&ob, idatBuf + 2, idatLen - 2)) {
        FreePool(rawBuf);
        FreePool(idatBuf);
        return NULL;
    }
    FreePool(idatBuf);

    /* unfilter rows */
    png_unfilter(rawBuf, imgW, srcBpp, imgH);

    /* convert to RGBA output */
    UINTN outSize = (UINTN)imgW * imgH * 4;
    UINT8 *out = AllocatePool(outSize);
    if (!out) {
        FreePool(rawBuf);
        return NULL;
    }

    for (UINT32 y = 0; y < imgH; y++) {
        const UINT8 *row = rawBuf + y * stride + 1;
        UINT8       *dst = out + y * imgW * 4;
        for (UINT32 x = 0; x < imgW; x++) {
            dst[x * 4 + 0] = row[x * srcBpp + 0]; /* R */
            dst[x * 4 + 1] = row[x * srcBpp + 1]; /* G */
            dst[x * 4 + 2] = row[x * srcBpp + 2]; /* B */
            dst[x * 4 + 3] = (srcBpp == 4) ? row[x * srcBpp + 3] : 0xFF;
        }
    }

    FreePool(rawBuf);
    *outWidth  = imgW;
    *outHeight = imgH;
    return out; /* caller frees */
}

/* ─────────────────────────────────────────────
   blit_rgba_centered  (تُستخدم لـ DrawPNG فقط)
───────────────────────────────────────────── */
static void blit_rgba_centered(
    const UINT8 *rgba,
    UINT32 imgW,
    UINT32 imgH,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    UINT32 *fb    = (UINT32 *)gop->Mode->FrameBufferBase;
    UINTN   scrW  = gop->Mode->Info->HorizontalResolution;
    UINTN   scrH  = gop->Mode->Info->VerticalResolution;
    UINTN   pitch = gop->Mode->Info->PixelsPerScanLine;

    for (UINTN y = 0; y < scrH; y++) {
        UINT32 srcY = (UINT32)(((UINT64)y * imgH) / scrH);
        UINT32 *dst = fb + y * pitch;

        for (UINTN x = 0; x < scrW; x++) {
            UINT32 srcX = (UINT32)(((UINT64)x * imgW) / scrW);
            const UINT8 *p = rgba + ((UINTN)srcY * imgW + srcX) * 4;

            UINT8 R = p[0];
            UINT8 G = p[1];
            UINT8 B = p[2];
            UINT8 A = p[3];

            R = (UINT8)((R * A) >> 8);
            G = (UINT8)((G * A) >> 8);
            B = (UINT8)((B * A) >> 8);

            dst[x] = ((UINT32)R << 16) | ((UINT32)G << 8) | ((UINT32)B);
        }
    }
}

/* ─────────────────────────────────────────────
   load_png_frame
   يفتح ملف PNG واحد ويعيد مصفوفة RGBA.
   المُستدعي مسؤول عن تحرير الذاكرة بـ FreePool.
───────────────────────────────────────────── */
UINT8 *load_png_frame(
    EFI_FILE_HANDLE Root,
    CHAR16 *Path,
    UINT32 *outW,
    UINT32 *outH)
{
    EFI_FILE_HANDLE File;
    EFI_STATUS      st;

    st = uefi_call_wrapper(
        Root->Open, 5, Root, &File, Path,
        EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st)) return NULL;

    /* حجم الملف */
    EFI_FILE_INFO *fi;
    UINTN fiSz = sizeof(EFI_FILE_INFO) + 256;
    fi = AllocatePool(fiSz);
    if (!fi) { uefi_call_wrapper(File->Close,1,File); return NULL; }

    EFI_GUID fid = EFI_FILE_INFO_ID;
    st = uefi_call_wrapper(File->GetInfo, 4, File, &fid, &fiSz, fi);
    if (EFI_ERROR(st)) {
        FreePool(fi);
        uefi_call_wrapper(File->Close,1,File);
        return NULL;
    }
    UINTN fSz = (UINTN)fi->FileSize;
    FreePool(fi);

    UINT8 *buf = AllocatePool(fSz);
    if (!buf) { uefi_call_wrapper(File->Close,1,File); return NULL; }

    UINTN rd = fSz;
    st = uefi_call_wrapper(File->Read, 3, File, &rd, buf);
    uefi_call_wrapper(File->Close, 1, File);
    if (EFI_ERROR(st)) { FreePool(buf); return NULL; }

    UINT8 *rgba = png_decode(buf, fSz, outW, outH);
    FreePool(buf);
    return rgba; /* NULL إذا فشل التفكيك */
}

/* ─────────────────────────────────────────────
   DrawPNG  –  load PNG from disk and blit centered
───────────────────────────────────────────── */
EFI_STATUS DrawPNG(
    EFI_FILE_HANDLE Root,
    CHAR16 *FileName,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
    EFI_FILE_HANDLE File;
    EFI_STATUS Status;

    Status = uefi_call_wrapper(
        Root->Open, 5, Root, &File, FileName,
        EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    /* get file size via GetInfo */
    EFI_FILE_INFO *fi;
    UINTN          fiSize = sizeof(EFI_FILE_INFO) + 256;
    fi = AllocatePool(fiSize);
    if (!fi) { uefi_call_wrapper(File->Close,1,File); return EFI_OUT_OF_RESOURCES; }

    EFI_GUID FileInfoGuid = EFI_FILE_INFO_ID;
    Status = uefi_call_wrapper(File->GetInfo, 4, File, &FileInfoGuid, &fiSize, fi);
    if (EFI_ERROR(Status)) {
        FreePool(fi);
        uefi_call_wrapper(File->Close,1,File);
        return Status;
    }
    UINTN fileSize = (UINTN)fi->FileSize;
    FreePool(fi);

    UINT8 *fileBuf = AllocatePool(fileSize);
    if (!fileBuf) {
        uefi_call_wrapper(File->Close,1,File);
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN readSize = fileSize;
    Status = uefi_call_wrapper(File->Read, 3, File, &readSize, fileBuf);
    uefi_call_wrapper(File->Close, 1, File);
    if (EFI_ERROR(Status)) {
        FreePool(fileBuf);
        return Status;
    }

    UINT32 imgW = 0, imgH = 0;
    UINT8 *rgba = png_decode(fileBuf, fileSize, &imgW, &imgH);
    FreePool(fileBuf);

    if (!rgba) {
        return EFI_LOAD_ERROR;
    }

    UINTN ScreenWidth  = gop->Mode->Info->HorizontalResolution;
    UINTN ScreenHeight = gop->Mode->Info->VerticalResolution;

    Print(L"Screen=%dx%d Image=%dx%d\n",
          ScreenWidth, ScreenHeight, imgW, imgH);

    /* امسح الشاشة بالأسود أولاً (نحتاج clear_screen من محرك_التمهيد) */
    /* clear_screen(gop);  - ستُستدعى من الخارج إذا لزم */

    /* ارسم الصورة بحجمها الحقيقي في منتصف الشاشة */
    blit_rgba_centered(rgba, imgW, imgH, gop);

    FreePool(rgba);

    return EFI_SUCCESS;
}
