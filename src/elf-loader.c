//
// Created by Thang Cao on 6/11/24.
//
#include <ros.h>
#include <elf.h>
#include <log.h>
#include "elf-loader.h"
#include "elf-loader-config.h"

#ifndef DOX

typedef struct {
    Pointer data;
    Int     secIdx;
    Offset  relSecIdx;
} ELFSection_t;

typedef struct elf_exec{
    LOADER_FD_T fd;

    Size sections;
    Offset sectionTable;
    Offset sectionTableStrings;

    Size symbolCount;
    Offset symbolTable;
    Offset symbolTableStrings;
    Offset entry;

    ELFSection_t text;
    ELFSection_t rodata;
    ELFSection_t data;
    ELFSection_t bss;

    Pointer stack;

    const ELFEnv_t *env;
} ELFExec_t;

#endif

typedef enum {
    FoundERROR = 0,
    FoundSymTab = (1 << 0),
    FoundStrTab = (1 << 2),
    FoundText = (1 << 3),
    FoundRodata = (1 << 4),
    FoundData = (1 << 5),
    FoundBss = (1 << 6),
    FoundRelText = (1 << 7),
    FoundRelRodata = (1 << 8),
    FoundRelData = (1 << 9),
    FoundRelBss = (1 << 10),
    FoundValid = FoundSymTab | FoundStrTab,
    FoundExec = FoundValid | FoundText,
    FoundAll = FoundSymTab | FoundStrTab | FoundText | FoundRodata | FoundData
               | FoundBss | FoundRelText | FoundRelRodata | FoundRelData | FoundRelBss
} FindFlags_t;

// static int initElf(ELFExec_t *e, HFile f)
// {
//     Elf32_Ehdr h;
//     Elf32_Shdr sH;

// _trace("Loading %s \n", "TJHANG");

//     if (!f)
//         return -1;

//     kdump(0x8000);

//     // LOADER_CLEAR(e, sizeof(ELFExec_t));

//     // if (LOADER_READ(f, &h, sizeof(h)) != sizeof(h))
//     //     return -1;

//     // e->fd = f;

//     // if (LOADER_SEEK_FROM_START(e->fd, h.e_shoff + h.e_shstrndx * sizeof(sH)) != 0)
//     //     return -1;
//     // if (LOADER_READ(e->fd, &sH, sizeof(Elf32_Shdr)) != sizeof(Elf32_Shdr))
//     //     return -1;

//     // e->entry = h.e_entry;
//     // e->sections = h.e_shnum;
//     // e->sectionTable = h.e_shoff;
//     // e->sectionTableStrings = sH.sh_offset;

//     // /* TODO Check ELF validity */

//     return 0;
// }

// int exec_elf(const char *path, const ELFEnv_t *env)
// {
//     _trace("Loading %s \n", path);

//     ELFExec_t exec;
//     if (initElf(&exec, fs_open_file(path)) != 0) {
//         log_error("Invalid elf %s\n", path);
//         return -1;
//     }
//     // exec.env = env;
//     // if (IS_FLAGS_SET(loadSymbols(&exec), FoundValid)) {
//     //     int ret = -1;
//     //     if (relocateSections(&exec) == 0)
//     //         ret = jumpTo(&exec);
//     //     freeElf(&exec);
//     //     return ret;
//     // } else {
//     //     MSG("Invalid EXEC");
//     //     return -1;
//     // }
// }
