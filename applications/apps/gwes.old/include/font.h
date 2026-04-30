#include "types.h"

typedef struct HFONT {
    U32 id;
    U32 ref_count;
    U32 style;
    U32 size;
    U32 weight;
    U32 charset;
    char* name;
    void* base; // ttf data
    void* glyph_cache;      // cache
} HFONT;

typedef struct GLYPH {
    int width;
    int height;
    int advance;
    char* bitmap;
} GLYPH;
