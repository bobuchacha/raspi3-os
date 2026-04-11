#include "user_runtime.h"

DLL_EXPORT(Init);
DLL_EXPORT(Deinit);
DLL_EXPORT(hello_three_print);

static void hello_three_write_banner(unsigned long base) {
    char line[192];
    char* cursor = line;

    cursor = appendText(cursor, "hello_three.dll: hello from DLL three, base=0x");
    cursor = appendHex(cursor, base);
    *cursor = '\0';
    writeLine(line);
}

int Init(void* base) {
    (void)base;
    return 0;
}

int Deinit(void* base) {
    (void)base;
    writeLine("hello_three.dll: goodbye");
    return 0;
}

void hello_three_print(unsigned long base) {
    hello_three_write_banner(base);
}
