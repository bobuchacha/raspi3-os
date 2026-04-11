#include "user_runtime.h"
#include "app/gdi.h"

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
