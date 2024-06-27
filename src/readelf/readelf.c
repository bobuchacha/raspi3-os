//
// Created by Jeff on 6/26/2024.
//
#include <elf.h>
#include "elf-ident.h"

#define FLAG_ELF_HEADER     0x01
#define FLAG_PROGRAM_HEADER 0x02
#define FLAG_SECTION_HEADER 0x04

int check_elf_magic_num(const unsigned char *e_ident) {
    return e_ident[EI_MAG0] != ELFMAG0 ||
           e_ident[EI_MAG1] != ELFMAG1 ||
           e_ident[EI_MAG2] != ELFMAG2 ||
           e_ident[EI_MAG3] != ELFMAG3;
}
