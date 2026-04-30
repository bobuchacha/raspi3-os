#pragma once
#include <stdint.h>

typedef uint32_t u32;
typedef void* HFONT;

typedef struct {
    u32 x, y, width, height;
} Rect;

enum Style {
    STYLE_NORMAL = 0,
    STYLE_ITALIC = 1
};

enum TextDecoration {
    DECOR_NONE = 0,
    DECOR_UNDERLINE = 1,
    DECOR_STRIKETHROUGH = 2
};

extern "C" HFONT Gdi_loadFont(const char* ttfPath);
extern "C" void  Gdi_freeFont(HFONT font);

extern "C" void Gdi_renderText(const wchar_t* text,
    u32 size, u32 weight,
    u32 background, u32 foreground,
    enum Style style, enum TextDecoration decoration,
    void* buffer, u32 bufferSize);

extern "C" void Gdi_renderParagraph(const wchar_t* text,
    u32 size, u32 weight,
    u32 background, u32 foreground,
    enum Style style, enum TextDecoration decoration,
    Rect rect,
    void* buffer, u32 bufferSize, u32 bufferStride);