HFONT load_font(char* name);
GLYPH get_glyph(HFONT hFont, char character);

// render text string to buffer
void render(char* text, HFONT hFont, U32 font_size, U32 font_weight, U32 color, U32 background, U32 flags, void* buffer);