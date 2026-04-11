#include "user_runtime.h"
#include "app/windowkit.h"

#define WIDGETDEMO_BG 0x000B1220U
#define WIDGETDEMO_TICK_MSEC 20UL

typedef struct WidgetDemoStateStruct {
    WindowKitContext ui;
    HWND mainWindow;
    HWND introLabel;
    HWND countButton;
    HWND textBox;
    HWND listBox;
    HWND statusLabel;
    unsigned int clickCount;
    int shuttingDown;
} WidgetDemoState;

static WidgetDemoState g_widgetdemo_state;
static const char* g_widgetdemo_items = "Overview\nProcesses\nDrivers\nMemory";

static const char* widgetdemo_item_name(unsigned int index) {
    switch (index) {
    case 0U:
        return "Overview";
    case 1U:
        return "Processes";
    case 2U:
        return "Drivers";
    case 3U:
        return "Memory";
    default:
        return "Unknown";
    }
}

static void widgetdemo_update_status(void) {
    char status[ROS_KERNEL_GUI_WIDGET_TEXT_MAX];
    char* cursor = status;
    const WindowKitNode* textNode = windowKitFindNodeConst(&g_widgetdemo_state.ui, g_widgetdemo_state.textBox);
    const WindowKitNode* listNode = windowKitFindNodeConst(&g_widgetdemo_state.ui, g_widgetdemo_state.listBox);
    unsigned int selectedIndex = listNode ? listNode->value : 0U;

    cursor = appendText(cursor, "clicks=");
    cursor = appendUnsignedLong(cursor, g_widgetdemo_state.clickCount);
    cursor = appendText(cursor, " | list=");
    cursor = appendText(cursor, widgetdemo_item_name(selectedIndex));
    cursor = appendText(cursor, " | text=");
    cursor = appendText(cursor, textNode ? textNode->text : "");
    *cursor = '\0';
    windowKitSetText(&g_widgetdemo_state.ui, g_widgetdemo_state.statusLabel, status);
}

static long widgetdemo_main_proc(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    (void)hwnd;
    (void)wParam;
    (void)lParam;
    if (message == WM_CLOSE) {
        if (!g_widgetdemo_state.shuttingDown) {
            g_widgetdemo_state.shuttingDown = 1;
            windowKitDestroyAll(context);
            (void)windowKitUpdateWindow(context);
        }
        exitProcess(0);
    }

    return 0;
}

static long widgetdemo_button_proc(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    (void)context;
    (void)hwnd;
    (void)wParam;
    (void)lParam;

    if (message == WM_MOUSECLICKED) {
        g_widgetdemo_state.clickCount++;
        widgetdemo_update_status();
    }

    return 0;
}

static long widgetdemo_textbox_proc(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    (void)context;
    (void)hwnd;
    (void)wParam;
    (void)lParam;

    if (message == WM_CHANGED) {
        widgetdemo_update_status();
    }

    return 0;
}

static long widgetdemo_list_proc(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    (void)context;
    (void)hwnd;
    (void)wParam;
    (void)lParam;

    if (message == WM_CHANGED) {
        widgetdemo_update_status();
    }

    return 0;
}

static long widgetdemo_noop_proc(WindowKitContext* context, HWND hwnd, uint32_t message, uint32_t wParam, uint32_t lParam) {
    (void)context;
    (void)hwnd;
    (void)message;
    (void)wParam;
    (void)lParam;
    return 0;
}

static void widgetdemo_init_ui(void) {
    RosKernelGuiWidgetRect windowRect = { 72U, 72U, 360U, 260U };

    windowKitInitContext(&g_widgetdemo_state.ui, WIDGETDEMO_BG);
    g_widgetdemo_state.mainWindow = windowKitCreateWindow(
        &g_widgetdemo_state.ui,
        WINDOWKIT_NULL_HWND,
        WINDOWKIT_CLASS_WINDOW,
        "Windows Toolkit Demo",
        windowRect,
        WINDOWKIT_STYLE_VISIBLE |
        WINDOWKIT_STYLE_ACTIVE |
        WINDOWKIT_STYLE_MOVABLE |
        WINDOWKIT_STYLE_RESIZABLE |
        WINDOWKIT_STYLE_CLOSE_BUTTON,
        widgetdemo_main_proc);
    windowKitSetMinSize(&g_widgetdemo_state.ui, g_widgetdemo_state.mainWindow, 460U, 320U);
    windowKitSetColors(&g_widgetdemo_state.ui, g_widgetdemo_state.mainWindow, 0x00111B2EU, 0x00F8FAFCU, 0x001D4ED8U);

    g_widgetdemo_state.introLabel = windowKitCreateWindow(
        &g_widgetdemo_state.ui,
        g_widgetdemo_state.mainWindow,
        WINDOWKIT_CLASS_LABEL,
        "Drag, resize, type, click, select.",
        (RosKernelGuiWidgetRect) {
        16U, 16U, 220U, 18U
    },
        WINDOWKIT_STYLE_VISIBLE,
        widgetdemo_noop_proc);
    windowKitSetColors(&g_widgetdemo_state.ui, g_widgetdemo_state.introLabel, 0U, 0x00E2E8F0U, 0U);

    g_widgetdemo_state.countButton = windowKitCreateWindow(
        &g_widgetdemo_state.ui,
        g_widgetdemo_state.mainWindow,
        WINDOWKIT_CLASS_BUTTON,
        "Count Click",
        (RosKernelGuiWidgetRect) {
        16U, 48U, 128U, 28U
    },
        WINDOWKIT_STYLE_VISIBLE |
        WINDOWKIT_STYLE_ENABLED |
        WINDOWKIT_STYLE_BORDER,
        widgetdemo_button_proc);
    windowKitSetColors(&g_widgetdemo_state.ui, g_widgetdemo_state.countButton, 0x001D4ED8U, 0x00F8FAFCU, 0x0094A3B8U);

    g_widgetdemo_state.textBox = windowKitCreateWindow(
        &g_widgetdemo_state.ui,
        g_widgetdemo_state.mainWindow,
        WINDOWKIT_CLASS_TEXTBOX,
        "type here",
        (RosKernelGuiWidgetRect) {
        16U, 92U, 208U, 30U
    },
        WINDOWKIT_STYLE_VISIBLE |
        WINDOWKIT_STYLE_ENABLED |
        WINDOWKIT_STYLE_BORDER,
        widgetdemo_textbox_proc);
    windowKitSetColors(&g_widgetdemo_state.ui, g_widgetdemo_state.textBox, 0x000F172AU, 0x00F8FAFCU, 0x0038BDF8U);

    g_widgetdemo_state.listBox = windowKitCreateWindow(
        &g_widgetdemo_state.ui,
        g_widgetdemo_state.mainWindow,
        WINDOWKIT_CLASS_LIST,
        g_widgetdemo_items,
        (RosKernelGuiWidgetRect) {
        240U, 16U, 100U, 140U
    },
        WINDOWKIT_STYLE_VISIBLE |
        WINDOWKIT_STYLE_ENABLED |
        WINDOWKIT_STYLE_BORDER,
        widgetdemo_list_proc);
    windowKitSetColors(&g_widgetdemo_state.ui, g_widgetdemo_state.listBox, 0x000F172AU, 0x00CBD5E1U, 0x003B82F6U);
    windowKitSetValue(&g_widgetdemo_state.ui, g_widgetdemo_state.listBox, 0U);

    g_widgetdemo_state.statusLabel = windowKitCreateWindow(
        &g_widgetdemo_state.ui,
        g_widgetdemo_state.mainWindow,
        WINDOWKIT_CLASS_LABEL,
        "",
        (RosKernelGuiWidgetRect) {
        16U, 140U, 320U, 36U
    },
        WINDOWKIT_STYLE_VISIBLE,
        widgetdemo_noop_proc);
    windowKitSetColors(&g_widgetdemo_state.ui, g_widgetdemo_state.statusLabel, 0U, 0x00CBD5E1U, 0U);

    widgetdemo_update_status();
}

int AppMain(void) {
    unsigned long gui_base;

    gui_base = openSharedLibrary(ROS_GUI_PATH);
    if (gui_base == 0UL) {
        writeLine("widgetdemo.exe: failed to load gui.dll");
        exitProcess(1);
    }

    if (guiInit() != 0) {
        writeLine("widgetdemo.exe: gui init failed");
        exitProcess(1);
    }

    (void)windowKitShowDesktop(1UL);
    (void)guiShowSoftKeyboard(1UL);
    widgetdemo_init_ui();
    (void)windowKitUpdateWindow(&g_widgetdemo_state.ui);
    writeLine("widgetdemo.exe: window-proc demo ready");

    for (;;) {
        MSG message;

        while (windowKitGetMessage(&g_widgetdemo_state.ui, &message) > 0) {
            (void)windowKitDispatchMessage(&g_widgetdemo_state.ui, &message);
        }
        (void)windowKitUpdateWindow(&g_widgetdemo_state.ui);
        (void)sleepMs(WIDGETDEMO_TICK_MSEC);
    }
}