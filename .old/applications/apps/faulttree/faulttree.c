#include "user_runtime.h"

#define FAULTTREE_CHILD_ARG "child"
#define FAULTTREE_TICK_MSEC 200UL

static int faulttreeTextEquals(const char* left, const char* right) {
    if (!left || !right) {
        return 0;
    }

    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return 0;
        }
        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static void faulttreeWritePidLine(const char* label, long pid) {
    char line[160];
    char* cursor = line;

    cursor = appendText(cursor, "faulttree.exe: ");
    cursor = appendText(cursor, label);
    if (pid >= 0) {
        cursor = appendUnsignedLong(cursor, (unsigned long)pid);
    }
    else {
        cursor = appendText(cursor, "<none>");
    }
    *cursor = '\0';
    writeLine(line);
}

static void faulttreeRunChild(void) {
    char line[160];

    writeLine("faulttree.exe: child running");
    for (unsigned long tick = 1UL;; tick++) {
        char* cursor = line;

        cursor = appendText(cursor, "faulttree.exe: child heartbeat ");
        cursor = appendUnsignedLong(cursor, tick);
        *cursor = '\0';
        writeLine(line);
        (void)sleepMs(FAULTTREE_TICK_MSEC);
    }
}

static void faulttreeBusyWaitMs(unsigned long delayMs) {
    unsigned long start = getUptimeMs();

    while ((getUptimeMs() - start) < delayMs) {
    }
}

static void __attribute__((noreturn)) faulttreeTriggerFault(void) {
    volatile unsigned long* badPointer = (volatile unsigned long*)0UL;

    writeLine("faulttree.exe: parent triggering intentional fault");
    *badPointer = 0xBAD0UL;

    for (;;) {
        asm volatile("wfe" ::: "memory");
    }
}

int AppMain(void) {
    char args[64];
    long childPid;

    getTaskArgs(args, sizeof(args));
    if (faulttreeTextEquals(args, FAULTTREE_CHILD_ARG)) {
        faulttreeRunChild();
    }

    writeLine("faulttree.exe: parent starting fault-tree validation");
    childPid = spawnTask("/bin/faulttree.exe", "faulttree-child", FAULTTREE_CHILD_ARG);
    faulttreeWritePidLine("spawned child pid=", childPid);
    faulttreeBusyWaitMs(FAULTTREE_TICK_MSEC * 2UL);
    faulttreeTriggerFault();
}