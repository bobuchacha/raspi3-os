#include "user_runtime.h"

DLL_EXPORT(Init);
DLL_EXPORT(Deinit);
DLL_EXPORT(hello_one_print);

static void hello_one_write_banner(unsigned long base) {
    char line[192];
    char* cursor = line;

    cursor = appendText(cursor, "hello_one.dll: hello from DLL one, base=0x");
    cursor = appendHex(cursor, base);
    *cursor = '\0';
    writeLine(line);
}

int Init(void* base) {
    (void)base;
    writeLine("hello_one.dll: hello and I want to let you know that I am initialized");
    return 0;
}

int Deinit(void* base) {
    (void)base;
    writeLine("hello_one.dll: goodbye");
    return 0;
}

void hello_one_print(unsigned long base) {
    hello_one_write_banner(base);
}
