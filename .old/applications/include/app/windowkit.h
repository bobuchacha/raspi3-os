#ifndef ROS_APP_WINDOWKIT_H
#define ROS_APP_WINDOWKIT_H

#include "user_runtime.h"
#include "app/gui.h"
#include "app/kernel_gui.h"
#include "stdint.h"
#include "stdlib.h"

typedef RosKernelGuiDesktopPointerState WindowKitPointerState;
typedef RosKernelGuiDesktopWindow WindowKitWindow;
typedef RosKernelGuiDesktopFrame WindowKitFrame;

typedef struct WindowKitDisplayStateStruct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t pixelFormat;
    uint32_t bufferRows;
    uint32_t bandY;
    uint32_t bandHeight;
    uint32_t* pixels;
} WindowKitDisplayState;

static WindowKitDisplayState g_windowkit_display;

#define WINDOWKIT_NULL_HWND 0U
#define WINDOWKIT_MAX_NODES (ROS_KERNEL_GUI_WIDGET_WINDOW_MAX * (ROS_KERNEL_GUI_WIDGET_CONTROL_MAX + 1U))
#define WINDOWKIT_MESSAGE_QUEUE_MAX 64U

#define WINDOWKIT_CLASS_NONE 0U
#define WINDOWKIT_CLASS_WINDOW 1U
#define WINDOWKIT_CLASS_LABEL 2U
#define WINDOWKIT_CLASS_BUTTON 3U
#define WINDOWKIT_CLASS_TEXTBOX 4U
#define WINDOWKIT_CLASS_LIST 5U
#define WINDOWKIT_CLASS_PANEL 6U

#define WINDOWKIT_STYLE_VISIBLE (1U << 0)
#define WINDOWKIT_STYLE_ENABLED (1U << 1)
#define WINDOWKIT_STYLE_BORDER (1U << 2)
#define WINDOWKIT_STYLE_ACTIVE (1U << 3)
#define WINDOWKIT_STYLE_MOVABLE (1U << 4)
#define WINDOWKIT_STYLE_RESIZABLE (1U << 5)
#define WINDOWKIT_STYLE_CLOSE_BUTTON (1U << 6)

#define WINDOWKIT_TITLEBAR_HEIGHT 28U
#define WINDOWKIT_WINDOW_PADDING 12U
#define WINDOWKIT_BORDER_THICKNESS 2U
#define WINDOWKIT_RESIZE_GRIP 6U
#define WINDOWKIT_FONT_SCALE 2U
#define WINDOWKIT_FONT_WIDTH 3U
#define WINDOWKIT_FONT_HEIGHT 5U
#define WINDOWKIT_FONT_ADVANCE ((WINDOWKIT_FONT_WIDTH * WINDOWKIT_FONT_SCALE) + WINDOWKIT_FONT_SCALE)
#define WINDOWKIT_FONT_LINE_HEIGHT ((WINDOWKIT_FONT_HEIGHT * WINDOWKIT_FONT_SCALE) + WINDOWKIT_FONT_SCALE)
#define WINDOWKIT_LIST_ITEM_HEIGHT 16U
#define WINDOWKIT_STREAM_ROWS 8U

#define WM_NULL 0U
#define WM_CREATE 1U
#define WM_DESTROY 2U
#define WM_REPAINT 3U
#define WM_MOUSEMOVE 4U
#define WM_MOUSELEAVE 5U
#define WM_LBUTTONDOWN 6U
#define WM_LBUTTONUP 7U
#define WM_MOUSECLICKED 8U
#define WM_KEYDOWN 9U
#define WM_CLOSE 10U
#define WM_MOVE 11U
#define WM_SIZE 12U
#define WM_CHANGED 13U
#define WM_PAINT 14U

typedef uint32_t HWND;

struct WindowKitContextStruct;

typedef long (*WNDPROC)(struct WindowKitContextStruct* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam);

typedef struct WindowKitMessageStruct {
    HWND hwnd;
    uint32_t message;
    uint32_t wParam;
    uint32_t lParam;
} WindowKitMessage;

typedef WindowKitMessage MSG;

typedef struct WindowKitPaintStruct {
    struct WindowKitContextStruct* context;
    HWND hwnd;
    RosKernelGuiWidgetRect bounds;
    uint32_t backgroundColor;
    uint32_t foregroundColor;
    uint32_t accentColor;
} WindowKitPaint;

typedef WindowKitPaint PAINTSTRUCT;
typedef WindowKitPaint WindowKitDC;

typedef struct WindowKitNodeStruct {
    HWND handle;
    HWND parent;
    uint32_t classId;
    uint32_t style;
    RosKernelGuiWidgetRect bounds;
    uint32_t minWidth;
    uint32_t minHeight;
    uint32_t backgroundColor;
    uint32_t foregroundColor;
    uint32_t accentColor;
    uint32_t value;
    char text[ROS_KERNEL_GUI_WIDGET_TEXT_MAX];
    WNDPROC proc;
} WindowKitNode;

typedef struct WindowKitContextStruct {
    uint32_t backgroundColor;
    uint32_t nextHandle;
    uint32_t nodeCount;
    uint32_t dirty;
    HWND hoverHandle;
    HWND focusHandle;
    HWND pointerCaptureHandle;
    HWND pressedHandle;
    HWND interactionHandle;
    uint32_t resizeEdges;
    int32_t dragStartX;
    int32_t dragStartY;
    int32_t dragOffsetX;
    int32_t dragOffsetY;
    RosKernelGuiWidgetRect dragBounds;
    RosKernelGuiPointerState pointer;
    HWND paintHandle;
    RosKernelGuiWidgetRect paintBounds;
    uint32_t queueHead;
    uint32_t queueTail;
    uint32_t queueCount;
    WindowKitMessage queue[WINDOWKIT_MESSAGE_QUEUE_MAX];
    WindowKitNode nodes[WINDOWKIT_MAX_NODES];
} WindowKitContext;

static inline int windowKitPeekMessage(WindowKitContext* context, MSG* message);
static inline long windowKitDispatchMessage(WindowKitContext* context, const MSG* message);
static inline void windowKitTranslateInputEvent(WindowKitContext* context, const RosKernelGuiInputEvent* event);
static inline long windowKitFlush(WindowKitContext* context);
static inline int windowKitGetMessage(WindowKitContext* context, MSG* message);

#define WINDOWKIT_GLYPH(a, b, c, d, e) \
    ((((uint16_t)(a) & 7U) << 12) | (((uint16_t)(b) & 7U) << 9) | (((uint16_t)(c) & 7U) << 6) | (((uint16_t)(d) & 7U) << 3) | ((uint16_t)(e) & 7U))

static inline void windowKitCopyText(char* destination, unsigned long size, const char* text) {
    unsigned long index = 0UL;

    if (!destination || size == 0UL) {
        return;
    }
    if (!text) {
        destination[0] = '\0';
        return;
    }

    while (index + 1UL < size && text[index] != '\0') {
        destination[index] = text[index];
        index++;
    }
    destination[index] = '\0';
}

static inline void windowKitResetBytes(void* buffer, unsigned long size) {
    unsigned char* bytes = (unsigned char*)buffer;

    if (!bytes) {
        return;
    }

    for (unsigned long index = 0UL; index < size; index++) {
        bytes[index] = 0U;
    }
}

static inline uint32_t windowKitMakeLParam(uint32_t low, uint32_t high) {
    return (low & 0xFFFFU) | ((high & 0xFFFFU) << 16);
}

static inline uint32_t windowKitLowWord(uint32_t value) {
    return value & 0xFFFFU;
}

static inline uint32_t windowKitHighWord(uint32_t value) {
    return (value >> 16) & 0xFFFFU;
}

static inline int windowKitPointInRect(uint32_t x, uint32_t y, uint32_t rectX, uint32_t rectY, uint32_t rectWidth, uint32_t rectHeight) {
    return x >= rectX && x < rectX + rectWidth && y >= rectY && y < rectY + rectHeight;
}

static inline int windowKitPointInTitleBar(const WindowKitWindow* window, uint32_t x, uint32_t y) {
    if (!window || !window->visible) {
        return 0;
    }

    return windowKitPointInRect(x, y, window->x, window->y, window->width, ROS_KERNEL_GUI_DESKTOP_TITLEBAR_HEIGHT);
}

static inline int windowKitLineTop(const WindowKitWindow* window, uint32_t lineIndex) {
    return (int)(window->y + ROS_KERNEL_GUI_DESKTOP_TITLEBAR_HEIGHT + ROS_KERNEL_GUI_DESKTOP_PADDING +
        (lineIndex * ROS_KERNEL_GUI_DESKTOP_LINE_HEIGHT));
}

static inline int windowKitPointInLine(const WindowKitWindow* window, uint32_t lineIndex, uint32_t x, uint32_t y) {
    uint32_t lineTop;

    if (!window || !window->visible) {
        return 0;
    }

    lineTop = (uint32_t)windowKitLineTop(window, lineIndex);
    return windowKitPointInRect(
        x,
        y,
        window->x + ROS_KERNEL_GUI_DESKTOP_PADDING,
        lineTop,
        window->width > (ROS_KERNEL_GUI_DESKTOP_PADDING * 2U) ? (window->width - (ROS_KERNEL_GUI_DESKTOP_PADDING * 2U)) : window->width,
        ROS_KERNEL_GUI_DESKTOP_LINE_HEIGHT);
}

static inline uint32_t windowKitEncodeColor(const WindowKitDisplayState* display, uint32_t color) {
    if (!display || display->pixelFormat != ROS_KERNEL_GUI_PIXEL_FORMAT_XBGR8888) {
        return color;
    }

    return ((color & 0x0000FFU) << 16) | (color & 0x00FF00U) | ((color & 0x00FF0000U) >> 16);
}

static inline int windowKitEnsureDisplay(void) {
    RosKernelGuiDisplayInfo info;
    uint32_t* pixels;
    unsigned long bytes;
    uint32_t bufferRows;

    if (guiQueryDisplayInfo(&info) != 0 || info.version != ROS_KERNEL_GUI_DISPLAY_INFO_VERSION ||
        info.width == 0U || info.height == 0U || info.pitch < info.width * sizeof(uint32_t)) {
        return -1;
    }

    bufferRows = info.height < WINDOWKIT_STREAM_ROWS ? info.height : WINDOWKIT_STREAM_ROWS;

    if (g_windowkit_display.pixels && g_windowkit_display.width == info.width &&
        g_windowkit_display.height == info.height && g_windowkit_display.pitch == info.pitch &&
        g_windowkit_display.pixelFormat == info.pixel_format && g_windowkit_display.bufferRows == bufferRows) {
        return 0;
    }

    if (g_windowkit_display.pixels) {
        free(g_windowkit_display.pixels);
        g_windowkit_display.pixels = 0;
    }

    bytes = (unsigned long)info.pitch * (unsigned long)bufferRows;
    pixels = (uint32_t*)malloc(bytes);
    if (!pixels) {
        windowKitResetBytes(&g_windowkit_display, sizeof(g_windowkit_display));
        return -1;
    }

    g_windowkit_display.width = info.width;
    g_windowkit_display.height = info.height;
    g_windowkit_display.pitch = info.pitch;
    g_windowkit_display.pixelFormat = info.pixel_format;
    g_windowkit_display.bufferRows = bufferRows;
    g_windowkit_display.bandY = 0U;
    g_windowkit_display.bandHeight = bufferRows;
    g_windowkit_display.pixels = pixels;
    return 0;
}

static inline void windowKitSetBand(uint32_t bandY, uint32_t bandHeight) {
    g_windowkit_display.bandY = bandY;
    g_windowkit_display.bandHeight = bandHeight;
}

static inline void windowKitClearSurface(uint32_t color) {
    uint32_t encoded = windowKitEncodeColor(&g_windowkit_display, color);
    uint32_t pitchPixels = g_windowkit_display.pitch / sizeof(uint32_t);

    for (uint32_t y = 0U; y < g_windowkit_display.bandHeight; y++) {
        uint32_t* row = g_windowkit_display.pixels + ((unsigned long)y * pitchPixels);
        for (uint32_t x = 0U; x < g_windowkit_display.width; x++) {
            row[x] = encoded;
        }
    }
}

static inline void windowKitFillRect(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
    uint32_t encoded;
    uint32_t pitchPixels;
    int32_t bandStart;
    int32_t bandEnd;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (!g_windowkit_display.pixels || width <= 0 || height <= 0) {
        return;
    }

    x0 = x < 0 ? 0 : x;
    bandStart = (int32_t)g_windowkit_display.bandY;
    bandEnd = bandStart + (int32_t)g_windowkit_display.bandHeight;
    y0 = y < bandStart ? bandStart : y;
    x1 = x + width;
    y1 = y + height;
    if (x1 > (int32_t)g_windowkit_display.width) {
        x1 = (int32_t)g_windowkit_display.width;
    }
    if (y1 > bandEnd) {
        y1 = bandEnd;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    encoded = windowKitEncodeColor(&g_windowkit_display, color);
    pitchPixels = g_windowkit_display.pitch / sizeof(uint32_t);
    for (int32_t row = y0; row < y1; row++) {
        uint32_t* destination = g_windowkit_display.pixels + ((unsigned long)(row - bandStart) * pitchPixels) + (unsigned long)x0;
        for (int32_t column = x0; column < x1; column++) {
            destination[column - x0] = encoded;
        }
    }
}

static inline void windowKitStrokeRect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t thickness, uint32_t color) {
    if (thickness <= 0 || width <= 0 || height <= 0) {
        return;
    }

    windowKitFillRect(x, y, width, thickness, color);
    windowKitFillRect(x, y + height - thickness, width, thickness, color);
    windowKitFillRect(x, y, thickness, height, color);
    windowKitFillRect(x + width - thickness, y, thickness, height, color);
}

static inline uint16_t windowKitGlyphBits(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - ('a' - 'A'));
    }

    switch (ch) {
    case 'A': return WINDOWKIT_GLYPH(7, 5, 7, 5, 5);
    case 'B': return WINDOWKIT_GLYPH(6, 5, 6, 5, 6);
    case 'C': return WINDOWKIT_GLYPH(7, 4, 4, 4, 7);
    case 'D': return WINDOWKIT_GLYPH(6, 5, 5, 5, 6);
    case 'E': return WINDOWKIT_GLYPH(7, 4, 6, 4, 7);
    case 'F': return WINDOWKIT_GLYPH(7, 4, 6, 4, 4);
    case 'G': return WINDOWKIT_GLYPH(7, 4, 5, 5, 7);
    case 'H': return WINDOWKIT_GLYPH(5, 5, 7, 5, 5);
    case 'I': return WINDOWKIT_GLYPH(7, 2, 2, 2, 7);
    case 'J': return WINDOWKIT_GLYPH(1, 1, 1, 5, 7);
    case 'K': return WINDOWKIT_GLYPH(5, 5, 6, 5, 5);
    case 'L': return WINDOWKIT_GLYPH(4, 4, 4, 4, 7);
    case 'M': return WINDOWKIT_GLYPH(5, 7, 7, 5, 5);
    case 'N': return WINDOWKIT_GLYPH(5, 7, 7, 7, 5);
    case 'O': return WINDOWKIT_GLYPH(7, 5, 5, 5, 7);
    case 'P': return WINDOWKIT_GLYPH(7, 5, 7, 4, 4);
    case 'Q': return WINDOWKIT_GLYPH(7, 5, 5, 7, 1);
    case 'R': return WINDOWKIT_GLYPH(7, 5, 7, 6, 5);
    case 'S': return WINDOWKIT_GLYPH(7, 4, 7, 1, 7);
    case 'T': return WINDOWKIT_GLYPH(7, 2, 2, 2, 2);
    case 'U': return WINDOWKIT_GLYPH(5, 5, 5, 5, 7);
    case 'V': return WINDOWKIT_GLYPH(5, 5, 5, 5, 2);
    case 'W': return WINDOWKIT_GLYPH(5, 5, 7, 7, 5);
    case 'X': return WINDOWKIT_GLYPH(5, 5, 2, 5, 5);
    case 'Y': return WINDOWKIT_GLYPH(5, 5, 2, 2, 2);
    case 'Z': return WINDOWKIT_GLYPH(7, 1, 2, 4, 7);
    case '0': return WINDOWKIT_GLYPH(7, 5, 5, 5, 7);
    case '1': return WINDOWKIT_GLYPH(2, 6, 2, 2, 7);
    case '2': return WINDOWKIT_GLYPH(7, 1, 7, 4, 7);
    case '3': return WINDOWKIT_GLYPH(7, 1, 7, 1, 7);
    case '4': return WINDOWKIT_GLYPH(5, 5, 7, 1, 1);
    case '5': return WINDOWKIT_GLYPH(7, 4, 7, 1, 7);
    case '6': return WINDOWKIT_GLYPH(7, 4, 7, 5, 7);
    case '7': return WINDOWKIT_GLYPH(7, 1, 1, 1, 1);
    case '8': return WINDOWKIT_GLYPH(7, 5, 7, 5, 7);
    case '9': return WINDOWKIT_GLYPH(7, 5, 7, 1, 7);
    case '-': return WINDOWKIT_GLYPH(0, 0, 7, 0, 0);
    case '.': return WINDOWKIT_GLYPH(0, 0, 0, 0, 2);
    case ':': return WINDOWKIT_GLYPH(0, 2, 0, 2, 0);
    case '/': return WINDOWKIT_GLYPH(1, 1, 2, 4, 4);
    case '[': return WINDOWKIT_GLYPH(6, 4, 4, 4, 6);
    case ']': return WINDOWKIT_GLYPH(3, 1, 1, 1, 3);
    case '(': return WINDOWKIT_GLYPH(2, 4, 4, 4, 2);
    case ')': return WINDOWKIT_GLYPH(2, 1, 1, 1, 2);
    case '<': return WINDOWKIT_GLYPH(1, 2, 4, 2, 1);
    case '>': return WINDOWKIT_GLYPH(4, 2, 1, 2, 4);
    case '_': return WINDOWKIT_GLYPH(0, 0, 0, 0, 7);
    case '=': return WINDOWKIT_GLYPH(0, 7, 0, 7, 0);
    case '!': return WINDOWKIT_GLYPH(2, 2, 2, 0, 2);
    case '?': return WINDOWKIT_GLYPH(7, 1, 2, 0, 2);
    case ',': return WINDOWKIT_GLYPH(0, 0, 0, 2, 4);
    case '+': return WINDOWKIT_GLYPH(0, 2, 7, 2, 0);
    case '*': return WINDOWKIT_GLYPH(0, 5, 2, 5, 0);
    case '\'': return WINDOWKIT_GLYPH(2, 2, 0, 0, 0);
    case '|': return WINDOWKIT_GLYPH(2, 2, 2, 2, 2);
    case ' ': return WINDOWKIT_GLYPH(0, 0, 0, 0, 0);
    default: return WINDOWKIT_GLYPH(7, 1, 2, 0, 2);
    }
}

static inline void windowKitDrawGlyph(int32_t x, int32_t y, char ch, uint32_t color) {
    uint16_t bits = windowKitGlyphBits(ch);

    for (uint32_t row = 0U; row < WINDOWKIT_FONT_HEIGHT; row++) {
        uint32_t rowBits = (bits >> (12U - (row * 3U))) & 7U;
        for (uint32_t column = 0U; column < WINDOWKIT_FONT_WIDTH; column++) {
            if ((rowBits & (1U << (WINDOWKIT_FONT_WIDTH - 1U - column))) != 0U) {
                windowKitFillRect(
                    x + (int32_t)(column * WINDOWKIT_FONT_SCALE),
                    y + (int32_t)(row * WINDOWKIT_FONT_SCALE),
                    (int32_t)WINDOWKIT_FONT_SCALE,
                    (int32_t)WINDOWKIT_FONT_SCALE,
                    color);
            }
        }
    }
}

static inline void windowKitDrawText(int32_t x, int32_t y, const char* text, uint32_t color) {
    int32_t cursorX = x;
    int32_t cursorY = y;

    while (text && *text != '\0') {
        if (*text == '\n') {
            cursorX = x;
            cursorY += (int32_t)WINDOWKIT_FONT_LINE_HEIGHT;
        }
        else {
            windowKitDrawGlyph(cursorX, cursorY, *text, color);
            cursorX += (int32_t)WINDOWKIT_FONT_ADVANCE;
        }
        text++;
    }
}

static inline int windowKitPresentSurface(void) {
    RosKernelGuiPresentBuffer buffer;

    if (!g_windowkit_display.pixels || g_windowkit_display.bandHeight == 0U) {
        return -1;
    }

    buffer.version = ROS_KERNEL_GUI_PRESENT_BUFFER_VERSION;
    buffer.x = 0U;
    buffer.y = g_windowkit_display.bandY;
    buffer.width = g_windowkit_display.width;
    buffer.height = g_windowkit_display.bandHeight;
    buffer.pitch = g_windowkit_display.pitch;
    buffer.pixels = (uint64_t)(uintptr_t)g_windowkit_display.pixels;
    buffer.reserved = 0U;
    return guiPresentPixels(&buffer);
}

static inline long windowKitQueryPointer(WindowKitPointerState* state) {
    return guiQueryPointer(state);
}

static inline void windowKitResetFrame(WindowKitFrame* frame, uint32_t backgroundColor) {
    if (!frame) {
        return;
    }

    windowKitResetBytes(frame, sizeof(*frame));
    frame->version = ROS_KERNEL_GUI_DESKTOP_FRAME_VERSION;
    frame->backgroundColor = backgroundColor;
}

static inline WindowKitWindow* windowKitAddWindow(
    WindowKitFrame* frame,
    const char* title,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t accentColor
) {
    WindowKitWindow* window;

    if (!frame || frame->windowCount >= ROS_KERNEL_GUI_DESKTOP_WINDOW_MAX) {
        return 0;
    }

    window = &frame->windows[frame->windowCount++];
    window->visible = 1U;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->accentColor = accentColor;
    window->lineCount = 0U;
    windowKitCopyText(window->title, sizeof(window->title), title);
    return window;
}

static inline void windowKitAppendLine(WindowKitWindow* window, const char* text) {
    if (!window || window->lineCount >= ROS_KERNEL_GUI_DESKTOP_WINDOW_LINE_MAX) {
        return;
    }

    windowKitCopyText(window->lines[window->lineCount], sizeof(window->lines[window->lineCount]), text);
    window->lineCount++;
}

static inline long windowKitPresent(const WindowKitFrame* frame) {
    if (!frame || windowKitEnsureDisplay() != 0) {
        return -1;
    }

    for (uint32_t bandY = 0U; bandY < g_windowkit_display.height; bandY += g_windowkit_display.bufferRows) {
        uint32_t bandHeight = g_windowkit_display.height - bandY;

        if (bandHeight > g_windowkit_display.bufferRows) {
            bandHeight = g_windowkit_display.bufferRows;
        }

        windowKitSetBand(bandY, bandHeight);
        windowKitClearSurface(frame->backgroundColor);
        for (uint32_t index = 0U; index < frame->windowCount; index++) {
            const WindowKitWindow* window = &frame->windows[index];
            int32_t clientTop;

            if (window->visible == 0U) {
                continue;
            }

            windowKitFillRect((int32_t)window->x, (int32_t)window->y, (int32_t)window->width, (int32_t)window->height, 0x00131D2CU);
            windowKitStrokeRect((int32_t)window->x, (int32_t)window->y, (int32_t)window->width, (int32_t)window->height, 2, window->accentColor);
            windowKitFillRect((int32_t)window->x, (int32_t)window->y, (int32_t)window->width, (int32_t)ROS_KERNEL_GUI_DESKTOP_TITLEBAR_HEIGHT, window->accentColor);
            windowKitDrawText((int32_t)window->x + 8, (int32_t)window->y + 6, window->title, 0x00F8FAFCU);

            clientTop = (int32_t)window->y + (int32_t)ROS_KERNEL_GUI_DESKTOP_TITLEBAR_HEIGHT + (int32_t)ROS_KERNEL_GUI_DESKTOP_PADDING;
            for (uint32_t line = 0U; line < window->lineCount; line++) {
                windowKitDrawText(
                    (int32_t)window->x + (int32_t)ROS_KERNEL_GUI_DESKTOP_PADDING,
                    clientTop + (int32_t)(line * ROS_KERNEL_GUI_DESKTOP_LINE_HEIGHT),
                    window->lines[line],
                    0x00E2E8F0U);
            }
        }

        if (windowKitPresentSurface() != 0) {
            return -1;
        }
    }

    return 0;
}

static inline long windowKitShowDesktop(unsigned long visible) {
    (void)visible;
    return 0;
}

static inline WindowKitNode* windowKitFindNode(WindowKitContext* context, HWND hwnd) {
    if (!context || hwnd == WINDOWKIT_NULL_HWND) {
        return 0;
    }

    for (uint32_t index = 0U; index < WINDOWKIT_MAX_NODES; index++) {
        if (context->nodes[index].handle == hwnd) {
            return &context->nodes[index];
        }
    }

    return 0;
}

static inline const WindowKitNode* windowKitFindNodeConst(const WindowKitContext* context, HWND hwnd) {
    if (!context || hwnd == WINDOWKIT_NULL_HWND) {
        return 0;
    }

    for (uint32_t index = 0U; index < WINDOWKIT_MAX_NODES; index++) {
        if (context->nodes[index].handle == hwnd) {
            return &context->nodes[index];
        }
    }

    return 0;
}

static inline HWND windowKitRootHandle(const WindowKitContext* context, HWND hwnd) {
    const WindowKitNode* node = windowKitFindNodeConst(context, hwnd);

    while (node && node->parent != WINDOWKIT_NULL_HWND) {
        node = windowKitFindNodeConst(context, node->parent);
    }

    return node ? node->handle : WINDOWKIT_NULL_HWND;
}

static inline RosKernelGuiWidgetRect windowKitAbsoluteBounds(const WindowKitContext* context, const WindowKitNode* node) {
    RosKernelGuiWidgetRect result = { 0U, 0U, 0U, 0U };

    if (!node) {
        return result;
    }
    if (node->parent == WINDOWKIT_NULL_HWND) {
        return node->bounds;
    }

    {
        const WindowKitNode* parent = windowKitFindNodeConst(context, node->parent);
        RosKernelGuiWidgetRect parentBounds = windowKitAbsoluteBounds(context, parent);

        result.x = parentBounds.x + WINDOWKIT_WINDOW_PADDING + node->bounds.x;
        result.y = parentBounds.y + WINDOWKIT_TITLEBAR_HEIGHT + WINDOWKIT_WINDOW_PADDING + node->bounds.y;
        result.width = node->bounds.width;
        result.height = node->bounds.height;
    }
    return result;
}

static inline RosKernelGuiWidgetRect windowKitPaintBounds(const WindowKitContext* context, const WindowKitNode* node) {
    RosKernelGuiWidgetRect result = windowKitAbsoluteBounds(context, node);

    if (!node) {
        return result;
    }

    if (node->parent == WINDOWKIT_NULL_HWND) {
        result.x += WINDOWKIT_WINDOW_PADDING;
        result.y += WINDOWKIT_TITLEBAR_HEIGHT + WINDOWKIT_WINDOW_PADDING;
        result.width = result.width > (WINDOWKIT_WINDOW_PADDING * 2U) ? result.width - (WINDOWKIT_WINDOW_PADDING * 2U) : 0U;
        result.height = result.height > (WINDOWKIT_TITLEBAR_HEIGHT + (WINDOWKIT_WINDOW_PADDING * 2U)) ? result.height - (WINDOWKIT_TITLEBAR_HEIGHT + (WINDOWKIT_WINDOW_PADDING * 2U)) : 0U;
    }

    return result;
}

static inline uint32_t windowKitDefaultBackground(uint32_t classId) {
    switch (classId) {
    case WINDOWKIT_CLASS_WINDOW:
        return 0x00111B2EU;
    case WINDOWKIT_CLASS_BUTTON:
        return 0x001D4ED8U;
    case WINDOWKIT_CLASS_TEXTBOX:
        return 0x000F172AU;
    case WINDOWKIT_CLASS_LIST:
        return 0x00111B2EU;
    case WINDOWKIT_CLASS_PANEL:
        return 0x00161F2BU;
    default:
        return 0U;
    }
}

static inline uint32_t windowKitDefaultForeground(uint32_t classId) {
    switch (classId) {
    case WINDOWKIT_CLASS_LABEL:
        return 0x00E2E8F0U;
    case WINDOWKIT_CLASS_LIST:
        return 0x00CBD5E1U;
    default:
        return 0x00F8FAFCU;
    }
}

static inline uint32_t windowKitDefaultAccent(uint32_t classId) {
    switch (classId) {
    case WINDOWKIT_CLASS_BUTTON:
        return 0x0094A3B8U;
    case WINDOWKIT_CLASS_TEXTBOX:
        return 0x0038BDF8U;
    case WINDOWKIT_CLASS_LIST:
        return 0x003B82F6U;
    case WINDOWKIT_CLASS_WINDOW:
        return 0x001D4ED8U;
    default:
        return 0x0064748BU;
    }
}

static inline void windowKitInitContext(WindowKitContext* context, uint32_t backgroundColor) {
    if (!context) {
        return;
    }

    windowKitResetBytes(context, sizeof(*context));
    context->backgroundColor = backgroundColor;
    context->nextHandle = 1U;
    context->dirty = 1U;
}

static inline void windowKitInvalidate(WindowKitContext* context) {
    if (context) {
        context->dirty = 1U;
    }
}

static inline long windowKitSendMessage(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    WindowKitNode* node = windowKitFindNode(context, hwnd);

    if (!node || !node->proc) {
        return 0;
    }

    return node->proc(context, hwnd, message, wParam, lParam);
}

static inline int windowKitPostMessage(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    WindowKitMessage* queued;

    if (!context || hwnd == WINDOWKIT_NULL_HWND || context->queueCount >= WINDOWKIT_MESSAGE_QUEUE_MAX) {
        return -1;
    }

    queued = &context->queue[context->queueTail];
    queued->hwnd = hwnd;
    queued->message = message;
    queued->wParam = wParam;
    queued->lParam = lParam;
    context->queueTail = (context->queueTail + 1U) % WINDOWKIT_MESSAGE_QUEUE_MAX;
    context->queueCount++;
    return 0;
}

static inline void windowKitSetText(WindowKitContext* context, HWND hwnd, const char* text) {
    WindowKitNode* node = windowKitFindNode(context, hwnd);

    if (!node) {
        return;
    }

    windowKitCopyText(node->text, sizeof(node->text), text);
    windowKitInvalidate(context);
}

static inline void windowKitSetValue(WindowKitContext* context, HWND hwnd, uint32_t value) {
    WindowKitNode* node = windowKitFindNode(context, hwnd);

    if (!node) {
        return;
    }

    node->value = value;
    windowKitInvalidate(context);
}

static inline void windowKitSetBounds(WindowKitContext* context, HWND hwnd, RosKernelGuiWidgetRect bounds) {
    WindowKitNode* node = windowKitFindNode(context, hwnd);

    if (!node) {
        return;
    }

    node->bounds = bounds;
    windowKitInvalidate(context);
}

static inline void windowKitSetColors(WindowKitContext* context, HWND hwnd, uint32_t backgroundColor, uint32_t foregroundColor, uint32_t accentColor) {
    WindowKitNode* node = windowKitFindNode(context, hwnd);

    if (!node) {
        return;
    }

    node->backgroundColor = backgroundColor;
    node->foregroundColor = foregroundColor;
    node->accentColor = accentColor;
    windowKitInvalidate(context);
}

static inline void windowKitSetMinSize(WindowKitContext* context, HWND hwnd, uint32_t minWidth, uint32_t minHeight) {
    WindowKitNode* node = windowKitFindNode(context, hwnd);

    if (!node) {
        return;
    }

    node->minWidth = minWidth;
    node->minHeight = minHeight;
}

static inline uint32_t windowKitQueuedMessageCount(const WindowKitContext* context) {
    return context ? context->queueCount : 0U;
}

static inline HWND windowKitFocusedWindow(const WindowKitContext* context) {
    return context ? context->focusHandle : WINDOWKIT_NULL_HWND;
}

static inline HWND windowKitHoveredWindow(const WindowKitContext* context) {
    return context ? context->hoverHandle : WINDOWKIT_NULL_HWND;
}

static inline WindowKitDC* windowKitBeginPaint(WindowKitContext* context, HWND hwnd, PAINTSTRUCT* paint) {
    const WindowKitNode* node;

    if (!context || !paint || hwnd == WINDOWKIT_NULL_HWND || context->paintHandle != hwnd) {
        return 0;
    }

    node = windowKitFindNodeConst(context, hwnd);
    if (!node) {
        return 0;
    }

    windowKitResetBytes(paint, sizeof(*paint));
    paint->context = context;
    paint->hwnd = hwnd;
    paint->bounds = context->paintBounds;
    paint->backgroundColor = node->backgroundColor;
    paint->foregroundColor = node->foregroundColor;
    paint->accentColor = node->accentColor;
    return paint;
}

static inline void windowKitEndPaint(WindowKitContext* context, HWND hwnd, const PAINTSTRUCT* paint) {
    (void)context;
    (void)hwnd;
    (void)paint;
}

static inline void windowKitDcFillRect(const WindowKitDC* dc, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color) {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (!dc || width <= 0 || height <= 0) {
        return;
    }

    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + width;
    y1 = y + height;
    if (x1 > (int32_t)dc->bounds.width) {
        x1 = (int32_t)dc->bounds.width;
    }
    if (y1 > (int32_t)dc->bounds.height) {
        y1 = (int32_t)dc->bounds.height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    windowKitFillRect((int32_t)dc->bounds.x + x0, (int32_t)dc->bounds.y + y0, x1 - x0, y1 - y0, color);
}

static inline void windowKitDcFrameRect(const WindowKitDC* dc, int32_t x, int32_t y, int32_t width, int32_t height, int32_t thickness, uint32_t color) {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (!dc || thickness <= 0 || width <= 0 || height <= 0) {
        return;
    }

    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + width;
    y1 = y + height;
    if (x1 > (int32_t)dc->bounds.width) {
        x1 = (int32_t)dc->bounds.width;
    }
    if (y1 > (int32_t)dc->bounds.height) {
        y1 = (int32_t)dc->bounds.height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    windowKitStrokeRect((int32_t)dc->bounds.x + x0, (int32_t)dc->bounds.y + y0, x1 - x0, y1 - y0, thickness, color);
}

static inline void windowKitDcDrawText(const WindowKitDC* dc, int32_t x, int32_t y, const char* text, uint32_t color) {
    if (!dc || !text) {
        return;
    }

    windowKitDrawText((int32_t)dc->bounds.x + x, (int32_t)dc->bounds.y + y, text, color);
}

static inline HWND windowKitCreateWindow(
    WindowKitContext* context,
    HWND parent,
    uint32_t classId,
    const char* text,
    RosKernelGuiWidgetRect bounds,
    uint32_t style,
    WNDPROC proc
) {
    WindowKitNode* node = 0;

    if (!context || classId == WINDOWKIT_CLASS_NONE) {
        return WINDOWKIT_NULL_HWND;
    }

    if (parent != WINDOWKIT_NULL_HWND) {
        WindowKitNode* parentNode = windowKitFindNode(context, parent);

        if (!parentNode || parentNode->parent != WINDOWKIT_NULL_HWND) {
            return WINDOWKIT_NULL_HWND;
        }
    }

    for (uint32_t index = 0U; index < WINDOWKIT_MAX_NODES; index++) {
        if (context->nodes[index].handle == WINDOWKIT_NULL_HWND) {
            node = &context->nodes[index];
            if (index >= context->nodeCount) {
                context->nodeCount = index + 1U;
            }
            break;
        }
    }
    if (!node) {
        return WINDOWKIT_NULL_HWND;
    }

    windowKitResetBytes(node, sizeof(*node));
    node->handle = context->nextHandle++;
    if (node->handle == WINDOWKIT_NULL_HWND) {
        node->handle = context->nextHandle++;
    }
    node->parent = parent;
    node->classId = classId;
    node->style = style;
    node->bounds = bounds;
    node->backgroundColor = windowKitDefaultBackground(classId);
    node->foregroundColor = windowKitDefaultForeground(classId);
    node->accentColor = windowKitDefaultAccent(classId);
    node->proc = proc;
    node->minWidth = 120U;
    node->minHeight = 80U;
    windowKitCopyText(node->text, sizeof(node->text), text);

    if ((style & WINDOWKIT_STYLE_ACTIVE) != 0U || context->focusHandle == WINDOWKIT_NULL_HWND) {
        context->focusHandle = node->handle;
    }

    (void)windowKitSendMessage(context, node->handle, WM_CREATE, 0U, 0U);
    windowKitInvalidate(context);
    return node->handle;
}

static inline void windowKitApplyTextboxKey(WindowKitNode* node, uint32_t key) {
    unsigned long length;

    if (!node) {
        return;
    }

    length = textLength(node->text);
    if ((key == 8U || key == 127U) && length > 0UL) {
        node->text[length - 1UL] = '\0';
        return;
    }
    if (key == '\r') {
        key = '\n';
    }
    if (key >= ' ' && key <= '~' && length + 1UL < sizeof(node->text)) {
        node->text[length] = (char)key;
        node->text[length + 1UL] = '\0';
    }
}

static inline void windowKitUpdateHover(WindowKitContext* context, HWND nextHover, uint32_t x, uint32_t y) {
    if (!context) {
        return;
    }

    if (context->hoverHandle != WINDOWKIT_NULL_HWND && context->hoverHandle != nextHover) {
        (void)windowKitPostMessage(context, context->hoverHandle, WM_MOUSELEAVE, 0U, windowKitMakeLParam(x, y));
    }
    context->hoverHandle = nextHover;
}

static inline uint32_t windowKitRootResizeEdges(const WindowKitNode* node, uint32_t x, uint32_t y) {
    uint32_t edges = ROS_KERNEL_GUI_WIDGET_RESIZE_NONE;

    if (!node || node->parent != WINDOWKIT_NULL_HWND || (node->style & WINDOWKIT_STYLE_RESIZABLE) == 0U) {
        return edges;
    }

    if (x <= node->bounds.x + WINDOWKIT_RESIZE_GRIP) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_LEFT;
    }
    else if (x + WINDOWKIT_RESIZE_GRIP >= node->bounds.x + node->bounds.width) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_RIGHT;
    }
    if (y <= node->bounds.y + WINDOWKIT_RESIZE_GRIP) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_TOP;
    }
    else if (y + WINDOWKIT_RESIZE_GRIP >= node->bounds.y + node->bounds.height) {
        edges |= ROS_KERNEL_GUI_WIDGET_RESIZE_BOTTOM;
    }

    return edges;
}

static inline int windowKitCloseHit(const WindowKitNode* node, uint32_t x, uint32_t y) {
    uint32_t buttonSize;
    uint32_t buttonX;
    uint32_t buttonY;

    if (!node || (node->style & WINDOWKIT_STYLE_CLOSE_BUTTON) == 0U) {
        return 0;
    }

    buttonSize = WINDOWKIT_TITLEBAR_HEIGHT > 10U ? WINDOWKIT_TITLEBAR_HEIGHT - 10U : WINDOWKIT_TITLEBAR_HEIGHT;
    buttonX = node->bounds.x + node->bounds.width - buttonSize - 6U;
    buttonY = node->bounds.y + 5U;
    return windowKitPointInRect(x, y, buttonX, buttonY, buttonSize, buttonSize);
}

static inline int windowKitTitleBarHit(const WindowKitNode* node, uint32_t x, uint32_t y) {
    if (!node || node->parent != WINDOWKIT_NULL_HWND) {
        return 0;
    }

    return windowKitPointInRect(x, y, node->bounds.x, node->bounds.y, node->bounds.width, WINDOWKIT_TITLEBAR_HEIGHT);
}

static inline unsigned int windowKitListItemIndexAt(const WindowKitContext* context, const WindowKitNode* node, uint32_t x, uint32_t y) {
    RosKernelGuiWidgetRect bounds = windowKitAbsoluteBounds(context, node);
    unsigned int index = 0U;

    if (y <= bounds.y + 4U) {
        return 0U;
    }

    index = (unsigned int)((y - (bounds.y + 4U)) / WINDOWKIT_LIST_ITEM_HEIGHT);
    return index;
}

static inline unsigned int windowKitTextLineCount(const char* text) {
    unsigned int count = 1U;

    if (!text || text[0] == '\0') {
        return 0U;
    }

    while (*text != '\0') {
        if (*text == '\n') {
            count++;
        }
        text++;
    }
    return count;
}

static inline void windowKitTextLineCopy(const char* text, unsigned int lineIndex, char* buffer, unsigned long size) {
    unsigned int currentLine = 0U;
    unsigned long cursor = 0UL;

    if (!buffer || size == 0UL) {
        return;
    }
    buffer[0] = '\0';
    if (!text) {
        return;
    }

    while (*text != '\0' && currentLine < lineIndex) {
        if (*text == '\n') {
            currentLine++;
        }
        text++;
    }
    if (currentLine != lineIndex) {
        return;
    }

    while (*text != '\0' && *text != '\n' && cursor + 1UL < size) {
        buffer[cursor++] = *text++;
    }
    buffer[cursor] = '\0';
}

static inline HWND windowKitHitTest(const WindowKitContext* context, uint32_t x, uint32_t y) {
    if (!context) {
        return WINDOWKIT_NULL_HWND;
    }

    for (int32_t index = (int32_t)context->nodeCount - 1; index >= 0; index--) {
        const WindowKitNode* node = &context->nodes[index];

        if (node->handle == WINDOWKIT_NULL_HWND || node->parent != WINDOWKIT_NULL_HWND || (node->style & WINDOWKIT_STYLE_VISIBLE) == 0U) {
            continue;
        }

        if (!windowKitPointInRect(x, y, node->bounds.x, node->bounds.y, node->bounds.width, node->bounds.height)) {
            continue;
        }

        for (int32_t childIndex = (int32_t)context->nodeCount - 1; childIndex >= 0; childIndex--) {
            const WindowKitNode* child = &context->nodes[childIndex];

            if (child->handle == WINDOWKIT_NULL_HWND || child->parent != node->handle || (child->style & WINDOWKIT_STYLE_VISIBLE) == 0U) {
                continue;
            }

            {
                RosKernelGuiWidgetRect absolute = windowKitAbsoluteBounds(context, child);

                if (windowKitPointInRect(x, y, absolute.x, absolute.y, absolute.width, absolute.height)) {
                    return child->handle;
                }
            }
        }

        return node->handle;
    }

    return WINDOWKIT_NULL_HWND;
}

static inline void windowKitDispatchQueue(WindowKitContext* context) {
    MSG message;

    while (context && context->queueCount > 0U) {
        if (windowKitPeekMessage(context, &message) <= 0) {
            break;
        }
        (void)windowKitDispatchMessage(context, &message);
    }
}

static inline void windowKitSendRepaint(WindowKitContext* context) {
    if (!context) {
        return;
    }

    for (uint32_t index = 0U; index < context->nodeCount; index++) {
        WindowKitNode* node = &context->nodes[index];

        if (node->handle == WINDOWKIT_NULL_HWND || (node->style & WINDOWKIT_STYLE_VISIBLE) == 0U) {
            continue;
        }

        (void)windowKitSendMessage(context, node->handle, WM_REPAINT, 0U, 0U);
    }
}

static inline int windowKitPeekMessage(WindowKitContext* context, MSG* message) {
    if (!context || !message || context->queueCount == 0U) {
        return 0;
    }

    *message = context->queue[context->queueHead];
    context->queueHead = (context->queueHead + 1U) % WINDOWKIT_MESSAGE_QUEUE_MAX;
    context->queueCount--;
    return 1;
}

static inline long windowKitDispatchMessage(WindowKitContext* context, const MSG* message) {
    if (!context || !message) {
        return -1;
    }

    return windowKitSendMessage(context, message->hwnd, message->message, message->wParam, message->lParam);
}

static inline void windowKitInvokePaint(WindowKitContext* context, const WindowKitNode* node) {
    HWND previousHandle;
    RosKernelGuiWidgetRect previousBounds;

    if (!context || !node || node->handle == WINDOWKIT_NULL_HWND || node->proc == 0) {
        return;
    }

    previousHandle = context->paintHandle;
    previousBounds = context->paintBounds;
    context->paintHandle = node->handle;
    context->paintBounds = windowKitPaintBounds(context, node);
    if (context->paintBounds.width > 0U && context->paintBounds.height > 0U) {
        (void)windowKitSendMessage(context, node->handle, WM_PAINT, 0U, 0U);
    }
    context->paintHandle = previousHandle;
    context->paintBounds = previousBounds;
}

static inline void windowKitRenderList(const WindowKitContext* context, const WindowKitNode* node) {
    RosKernelGuiWidgetRect absolute = windowKitAbsoluteBounds(context, node);
    unsigned int lineCount = windowKitTextLineCount(node->text);

    windowKitFillRect((int32_t)absolute.x, (int32_t)absolute.y, (int32_t)absolute.width, (int32_t)absolute.height, node->backgroundColor);
    windowKitStrokeRect((int32_t)absolute.x, (int32_t)absolute.y, (int32_t)absolute.width, (int32_t)absolute.height, 1, node->accentColor);
    for (unsigned int index = 0U; index < lineCount; index++) {
        char line[ROS_KERNEL_GUI_WIDGET_TEXT_MAX];
        int32_t lineY = (int32_t)absolute.y + 4 + (int32_t)(index * WINDOWKIT_LIST_ITEM_HEIGHT);

        windowKitTextLineCopy(node->text, index, line, sizeof(line));
        if (index == node->value) {
            windowKitFillRect((int32_t)absolute.x + 2, lineY - 1, (int32_t)absolute.width - 4, (int32_t)WINDOWKIT_LIST_ITEM_HEIGHT - 2, node->accentColor);
        }
        windowKitDrawText((int32_t)absolute.x + 6, lineY, line, node->foregroundColor);
    }
}

static inline void windowKitRenderNode(WindowKitContext* context, const WindowKitNode* node) {
    RosKernelGuiWidgetRect absolute;

    if (!node || node->handle == WINDOWKIT_NULL_HWND || (node->style & WINDOWKIT_STYLE_VISIBLE) == 0U || node->parent == WINDOWKIT_NULL_HWND) {
        return;
    }

    absolute = windowKitAbsoluteBounds(context, node);
    switch (node->classId) {
    case WINDOWKIT_CLASS_LABEL:
        windowKitDrawText((int32_t)absolute.x, (int32_t)absolute.y + 2, node->text, node->foregroundColor);
        break;
    case WINDOWKIT_CLASS_BUTTON:
        windowKitFillRect((int32_t)absolute.x, (int32_t)absolute.y, (int32_t)absolute.width, (int32_t)absolute.height, node->backgroundColor);
        windowKitStrokeRect((int32_t)absolute.x, (int32_t)absolute.y, (int32_t)absolute.width, (int32_t)absolute.height, 1, node->accentColor);
        windowKitDrawText((int32_t)absolute.x + 8, (int32_t)absolute.y + 8, node->text, node->foregroundColor);
        break;
    case WINDOWKIT_CLASS_TEXTBOX:
        windowKitFillRect((int32_t)absolute.x, (int32_t)absolute.y, (int32_t)absolute.width, (int32_t)absolute.height, node->backgroundColor);
        windowKitStrokeRect((int32_t)absolute.x, (int32_t)absolute.y, (int32_t)absolute.width, (int32_t)absolute.height, 1, node->handle == context->focusHandle ? node->accentColor : 0x0064748BU);
        windowKitDrawText((int32_t)absolute.x + 6, (int32_t)absolute.y + 8, node->text, node->foregroundColor);
        break;
    case WINDOWKIT_CLASS_LIST:
        windowKitRenderList(context, node);
        break;
    case WINDOWKIT_CLASS_PANEL:
        windowKitFillRect((int32_t)absolute.x, (int32_t)absolute.y, (int32_t)absolute.width, (int32_t)absolute.height, node->backgroundColor);
        if ((node->style & WINDOWKIT_STYLE_BORDER) != 0U) {
            windowKitStrokeRect((int32_t)absolute.x, (int32_t)absolute.y, (int32_t)absolute.width, (int32_t)absolute.height, 1, node->accentColor);
        }
        break;
    default:
        break;
    }

    windowKitInvokePaint(context, node);
}

static inline void windowKitRenderContext(WindowKitContext* context) {
    windowKitClearSurface(context->backgroundColor);
    for (uint32_t index = 0U; index < context->nodeCount; index++) {
        WindowKitNode* root = &context->nodes[index];

        if (root->handle == WINDOWKIT_NULL_HWND || root->parent != WINDOWKIT_NULL_HWND || (root->style & WINDOWKIT_STYLE_VISIBLE) == 0U) {
            continue;
        }

        windowKitFillRect((int32_t)root->bounds.x, (int32_t)root->bounds.y, (int32_t)root->bounds.width, (int32_t)root->bounds.height, root->backgroundColor);
        windowKitStrokeRect((int32_t)root->bounds.x, (int32_t)root->bounds.y, (int32_t)root->bounds.width, (int32_t)root->bounds.height, (int32_t)WINDOWKIT_BORDER_THICKNESS, root->handle == windowKitRootHandle(context, context->focusHandle) ? root->accentColor : 0x0064748BU);
        windowKitFillRect((int32_t)root->bounds.x, (int32_t)root->bounds.y, (int32_t)root->bounds.width, (int32_t)WINDOWKIT_TITLEBAR_HEIGHT, root->accentColor);
        windowKitDrawText((int32_t)root->bounds.x + 8, (int32_t)root->bounds.y + 7, root->text, 0x00F8FAFCU);
        if ((root->style & WINDOWKIT_STYLE_CLOSE_BUTTON) != 0U) {
            uint32_t buttonSize = WINDOWKIT_TITLEBAR_HEIGHT > 10U ? WINDOWKIT_TITLEBAR_HEIGHT - 10U : WINDOWKIT_TITLEBAR_HEIGHT;
            int32_t buttonX = (int32_t)(root->bounds.x + root->bounds.width - buttonSize - 6U);
            int32_t buttonY = (int32_t)root->bounds.y + 5;

            windowKitFillRect(buttonX, buttonY, (int32_t)buttonSize, (int32_t)buttonSize, 0x00B91C1CU);
            windowKitDrawText(buttonX + 5, buttonY + 4, "X", 0x00F8FAFCU);
        }

        windowKitInvokePaint(context, root);

        for (uint32_t childIndex = 0U; childIndex < context->nodeCount; childIndex++) {
            WindowKitNode* child = &context->nodes[childIndex];

            if (child->handle == WINDOWKIT_NULL_HWND || child->parent != root->handle || (child->style & WINDOWKIT_STYLE_VISIBLE) == 0U) {
                continue;
            }
            windowKitRenderNode(context, child);
        }
    }

}

static inline void windowKitCollectInput(WindowKitContext* context) {
    if (!context) {
        return;
    }

    for (;;) {
        RosKernelGuiInputEvent event;

        if (guiPollInputEvent(&event) != 0 || event.type == ROS_KERNEL_GUI_INPUT_EVENT_NONE) {
            break;
        }
        windowKitTranslateInputEvent(context, &event);
    }
}

static inline void windowKitClampRootBounds(WindowKitNode* node) {
    if (!node) {
        return;
    }

    if (node->bounds.width < node->minWidth) {
        node->bounds.width = node->minWidth;
    }
    if (node->bounds.height < node->minHeight) {
        node->bounds.height = node->minHeight;
    }
}

static inline void windowKitTranslateInputEvent(WindowKitContext* context, const RosKernelGuiInputEvent* event) {
    HWND target;
    WindowKitNode* node;

    if (!context || !event || event->type == ROS_KERNEL_GUI_INPUT_EVENT_NONE) {
        return;
    }

    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_LEAVE) {
        context->pointer.visible = 0U;
        context->pointer.pressed = 0U;
        windowKitUpdateHover(context, WINDOWKIT_NULL_HWND, event->x, event->y);
        context->interactionHandle = WINDOWKIT_NULL_HWND;
        context->pressedHandle = WINDOWKIT_NULL_HWND;
        context->resizeEdges = ROS_KERNEL_GUI_WIDGET_RESIZE_NONE;
        windowKitInvalidate(context);
        return;
    }

    if (event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE || event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_DOWN || event->type == ROS_KERNEL_GUI_INPUT_EVENT_POINTER_UP) {
        context->pointer.x = event->x;
        context->pointer.y = event->y;
        context->pointer.pressed = event->buttons;
        context->pointer.visible = 1U;
    }

    target = context->pointerCaptureHandle != WINDOWKIT_NULL_HWND ? context->pointerCaptureHandle : windowKitHitTest(context, event->x, event->y);
    node = windowKitFindNode(context, target);

    switch (event->type) {
    case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_MOVE:
        if (context->interactionHandle != WINDOWKIT_NULL_HWND) {
            WindowKitNode* root = windowKitFindNode(context, context->interactionHandle);

            if (root) {
                if (context->resizeEdges != ROS_KERNEL_GUI_WIDGET_RESIZE_NONE) {
                    int32_t dx = (int32_t)event->x - context->dragStartX;
                    int32_t dy = (int32_t)event->y - context->dragStartY;

                    root->bounds = context->dragBounds;
                    if ((context->resizeEdges & ROS_KERNEL_GUI_WIDGET_RESIZE_LEFT) != 0U) {
                        root->bounds.x = (uint32_t)((int32_t)context->dragBounds.x + dx);
                        root->bounds.width = (uint32_t)((int32_t)context->dragBounds.width - dx);
                    }
                    if ((context->resizeEdges & ROS_KERNEL_GUI_WIDGET_RESIZE_RIGHT) != 0U) {
                        root->bounds.width = (uint32_t)((int32_t)context->dragBounds.width + dx);
                    }
                    if ((context->resizeEdges & ROS_KERNEL_GUI_WIDGET_RESIZE_TOP) != 0U) {
                        root->bounds.y = (uint32_t)((int32_t)context->dragBounds.y + dy);
                        root->bounds.height = (uint32_t)((int32_t)context->dragBounds.height - dy);
                    }
                    if ((context->resizeEdges & ROS_KERNEL_GUI_WIDGET_RESIZE_BOTTOM) != 0U) {
                        root->bounds.height = (uint32_t)((int32_t)context->dragBounds.height + dy);
                    }
                    windowKitClampRootBounds(root);
                    (void)windowKitPostMessage(context, root->handle, WM_SIZE, 0U, windowKitMakeLParam(root->bounds.width, root->bounds.height));
                }
                else {
                    root->bounds.x = (uint32_t)((int32_t)event->x - context->dragOffsetX);
                    root->bounds.y = (uint32_t)((int32_t)event->y - context->dragOffsetY);
                    (void)windowKitPostMessage(context, root->handle, WM_MOVE, 0U, windowKitMakeLParam(root->bounds.x, root->bounds.y));
                }
                windowKitInvalidate(context);
            }
        }
        else {
            windowKitUpdateHover(context, target, event->x, event->y);
            if (target != WINDOWKIT_NULL_HWND) {
                (void)windowKitPostMessage(context, target, WM_MOUSEMOVE, event->buttons, windowKitMakeLParam(event->x, event->y));
            }
            windowKitInvalidate(context);
        }
        break;
    case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_DOWN:
        context->pressedHandle = target;
        if (target != WINDOWKIT_NULL_HWND) {
            context->focusHandle = target;
            if (node && node->parent == WINDOWKIT_NULL_HWND) {
                if (windowKitCloseHit(node, event->x, event->y)) {
                    (void)windowKitPostMessage(context, node->handle, WM_CLOSE, 0U, 0U);
                }
                else {
                    context->resizeEdges = windowKitRootResizeEdges(node, event->x, event->y);
                    if (context->resizeEdges != ROS_KERNEL_GUI_WIDGET_RESIZE_NONE) {
                        context->interactionHandle = node->handle;
                        context->dragStartX = (int32_t)event->x;
                        context->dragStartY = (int32_t)event->y;
                        context->dragBounds = node->bounds;
                    }
                    else if ((node->style & WINDOWKIT_STYLE_MOVABLE) != 0U && windowKitTitleBarHit(node, event->x, event->y)) {
                        context->interactionHandle = node->handle;
                        context->dragOffsetX = (int32_t)event->x - (int32_t)node->bounds.x;
                        context->dragOffsetY = (int32_t)event->y - (int32_t)node->bounds.y;
                        context->dragBounds = node->bounds;
                    }
                }
            }
            (void)windowKitPostMessage(context, target, WM_LBUTTONDOWN, event->buttons, windowKitMakeLParam(event->x, event->y));
        }
        windowKitInvalidate(context);
        break;
    case ROS_KERNEL_GUI_INPUT_EVENT_POINTER_UP: {
        HWND clickTarget = windowKitHitTest(context, event->x, event->y);

        if (target != WINDOWKIT_NULL_HWND) {
            (void)windowKitPostMessage(context, target, WM_LBUTTONUP, event->buttons, windowKitMakeLParam(event->x, event->y));
        }
        if (context->pressedHandle != WINDOWKIT_NULL_HWND && clickTarget == context->pressedHandle) {
            WindowKitNode* pressedNode = windowKitFindNode(context, context->pressedHandle);

            if (pressedNode) {
                if (pressedNode->classId == WINDOWKIT_CLASS_BUTTON) {
                    (void)windowKitPostMessage(context, pressedNode->handle, WM_MOUSECLICKED, 0U, windowKitMakeLParam(event->x, event->y));
                }
                else if (pressedNode->classId == WINDOWKIT_CLASS_LIST) {
                    unsigned int lineCount = windowKitTextLineCount(pressedNode->text);
                    unsigned int nextValue = windowKitListItemIndexAt(context, pressedNode, event->x, event->y);

                    if (lineCount > 0U && nextValue >= lineCount) {
                        nextValue = lineCount - 1U;
                    }
                    pressedNode->value = nextValue;
                    (void)windowKitPostMessage(context, pressedNode->handle, WM_CHANGED, pressedNode->value, 0U);
                }
            }
        }
        context->interactionHandle = WINDOWKIT_NULL_HWND;
        context->resizeEdges = ROS_KERNEL_GUI_WIDGET_RESIZE_NONE;
        context->pressedHandle = WINDOWKIT_NULL_HWND;
        windowKitUpdateHover(context, clickTarget, event->x, event->y);
        windowKitInvalidate(context);
        break;
    }
    case ROS_KERNEL_GUI_INPUT_EVENT_KEY_DOWN:
        if (context->focusHandle != WINDOWKIT_NULL_HWND) {
            WindowKitNode* focusNode = windowKitFindNode(context, context->focusHandle);

            if (focusNode && focusNode->classId == WINDOWKIT_CLASS_TEXTBOX) {
                windowKitApplyTextboxKey(focusNode, event->key);
                (void)windowKitPostMessage(context, focusNode->handle, WM_CHANGED, focusNode->value, event->key);
                windowKitInvalidate(context);
            }
            (void)windowKitPostMessage(context, context->focusHandle, WM_KEYDOWN, event->key, windowKitMakeLParam(context->pointer.x, context->pointer.y));
        }
        break;
    default:
        break;
    }
}

static inline long windowKitFlush(WindowKitContext* context) {
    if (!context || context->dirty == 0U) {
        return 0;
    }
    if (windowKitEnsureDisplay() != 0) {
        return -1;
    }

    windowKitSendRepaint(context);
    for (uint32_t bandY = 0U; bandY < g_windowkit_display.height; bandY += g_windowkit_display.bufferRows) {
        uint32_t bandHeight = g_windowkit_display.height - bandY;

        if (bandHeight > g_windowkit_display.bufferRows) {
            bandHeight = g_windowkit_display.bufferRows;
        }

        windowKitSetBand(bandY, bandHeight);
        windowKitRenderContext(context);
        if (windowKitPresentSurface() != 0) {
            return -1;
        }
    }
    context->dirty = 0U;
    return 0;
}

static inline long windowKitPumpMessages(WindowKitContext* context) {
    MSG message;

    if (!context) {
        return -1;
    }

    while (windowKitGetMessage(context, &message) > 0) {
        (void)windowKitDispatchMessage(context, &message);
    }
    return windowKitFlush(context);
}

static inline int windowKitGetMessage(WindowKitContext* context, MSG* message) {
    if (!context || !message) {
        return -1;
    }

    if (windowKitPeekMessage(context, message) > 0) {
        return 1;
    }

    // Translate only enough raw input to surface one queued message.
    // This preserves click ordering and prevents mouse-move bursts from
    // filling the fixed queue before button-up/click messages are posted.
    for (;;) {
        RosKernelGuiInputEvent event;

        if (guiPollInputEvent(&event) != 0 || event.type == ROS_KERNEL_GUI_INPUT_EVENT_NONE) {
            return 0;
        }
        windowKitTranslateInputEvent(context, &event);
        if (windowKitPeekMessage(context, message) > 0) {
            return 1;
        }
    }
}

static inline long windowKitUpdateWindow(WindowKitContext* context) {
    return windowKitFlush(context);
}

static inline void windowKitDestroyWindow(WindowKitContext* context, HWND hwnd) {
    WindowKitNode* node;

    if (!context || hwnd == WINDOWKIT_NULL_HWND) {
        return;
    }

    for (uint32_t index = 0U; index < context->nodeCount; index++) {
        if (context->nodes[index].handle != WINDOWKIT_NULL_HWND && context->nodes[index].parent == hwnd) {
            windowKitDestroyWindow(context, context->nodes[index].handle);
        }
    }

    node = windowKitFindNode(context, hwnd);
    if (!node) {
        return;
    }

    (void)windowKitSendMessage(context, hwnd, WM_DESTROY, 0U, 0U);
    if (context->hoverHandle == hwnd) {
        context->hoverHandle = WINDOWKIT_NULL_HWND;
    }
    if (context->focusHandle == hwnd) {
        context->focusHandle = WINDOWKIT_NULL_HWND;
    }
    if (context->pointerCaptureHandle == hwnd) {
        context->pointerCaptureHandle = WINDOWKIT_NULL_HWND;
    }
    if (context->pressedHandle == hwnd) {
        context->pressedHandle = WINDOWKIT_NULL_HWND;
    }
    if (context->interactionHandle == hwnd) {
        context->interactionHandle = WINDOWKIT_NULL_HWND;
        context->resizeEdges = ROS_KERNEL_GUI_WIDGET_RESIZE_NONE;
    }
    windowKitResetBytes(node, sizeof(*node));
    windowKitInvalidate(context);
}

static inline void windowKitDestroyAll(WindowKitContext* context) {
    if (!context) {
        return;
    }

    for (uint32_t index = 0U; index < context->nodeCount; index++) {
        if (context->nodes[index].handle != WINDOWKIT_NULL_HWND && context->nodes[index].parent == WINDOWKIT_NULL_HWND) {
            windowKitDestroyWindow(context, context->nodes[index].handle);
        }
    }
}

#endif