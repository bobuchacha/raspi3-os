#include "user_runtime.h"

#undef DLL_IMPORT_FUNCTION
#undef DECLARE

#define DLL_IMPORT_FUNCTION(module_name_literal, symbol_name_literal, function_type, slot_symbol) \
    function_type slot_symbol __attribute__((used, section(".data"))) = (function_type)0; \
    LDR_EMIT_IMPORT(module_name_literal, symbol_name_literal, #slot_symbol, slot_symbol)

#define DECLARE(ret, name, args, non, module, symbol) \
    typedef ret (*name##_fn) args; \
    DLL_IMPORT_FUNCTION(module, symbol, name##_fn, name)

#define ROS_GDI_NO_IMPORTS 1
#include "app/gdi.h"

DECLARE(long, GdiLoadFont, (const char* path, unsigned long pixel_height, RosGdiFont* font), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiLoadFont");
DECLARE(long, GdiUnloadFont, (RosGdiFont* font), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiUnloadFont");
DECLARE(long, GdiMeasureText, (const RosGdiFont* font, const char* text, unsigned long* width, unsigned long* height), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiMeasureText");
DECLARE(long, GdiDrawTextSurface, (const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long color), FROM, ROS_GDI_CLIENT_MODULE_NAME, "GdiDrawTextSurface");

long gwes_gdi_load_font(const char* path, unsigned long pixel_height, RosGdiFont* font) {
    return GdiLoadFont(path, pixel_height, font);
}

long gwes_gdi_unload_font(RosGdiFont* font) {
    return GdiUnloadFont(font);
}

long gwes_gdi_measure_text(const RosGdiFont* font, const char* text, unsigned long* width, unsigned long* height) {
    return GdiMeasureText(font, text, width, height);
}

long gwes_gdi_draw_text_surface(const RosGdiSurface* surface, const RosGdiFont* font, unsigned long x, unsigned long y, const char* text, unsigned long color) {
    return GdiDrawTextSurface(surface, font, x, y, text, color);
}
