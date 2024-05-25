//
// Created by Jeff on 5/24/2024.
//
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include "vmmexam.h"

typedef unsigned long long ULONG;

ULONG pgd[512];
ULONG pud[512];
ULONG pmd[512];
ULONG ptb[512];
ULONG mem_offset;
ULONG loaded_addr = 0x81000;

#define GET_PGD_ID(addr) (addr >> 39 & 0x1FF)    // get bit 39-47 of a number
#define GET_PUD_ID(addr) (addr >> 30 & 0x1FF) // get bit 30-38 of an address
#define GET_PMD_ID(addr) (addr >> 21 & 0x1FF) // get bit 21-29 of the address
#define GET_PAGE_ID(addr) (addr >> 12 & 0x1FF) // get bit 12-20 of the address

#define VA_MASK 0xFFFFFFFFFFFFF000
#define VA_MASK_BLOCK_ADDR 0x1FFFFF

FILE *f;
ULONG get_file_offset(ULONG val){    // get table base address in file from memory value of the file
    return (val & VA_MASK) - mem_offset;
}

ULONG get_pud_base(ULONG pgd_id){
    return get_file_offset(pgd[pgd_id]);
}
ULONG get_pmd_base(ULONG pud_id){
    return get_file_offset(pud[pud_id]);
}
ULONG get_block_address(ULONG pmd_id){
    return ((pmd[pmd_id] & 0xFFFFFFFFFFE00000));
}


ULONG get_physical_addr(ULONG va){
    ULONG pdg_id = GET_PGD_ID(va);
    ULONG pud_id = GET_PUD_ID(va);
    ULONG pmd_id = GET_PMD_ID(va);
    ULONG block_addr = get_block_address(pmd_id);
    ULONG block_offs = va & VA_MASK_BLOCK_ADDR;


    printf("-> pdg id %d, value 0x%x\n", pdg_id, pgd[pdg_id]);
    printf("-> pud id %d, value 0x%x\n", pud_id, pud[pud_id]);
    printf("-> pmg id %d, value 0x%x. \n", pmd_id, pmd[pmd_id]);
    printf("-\n");
    printf("-> PUD base is 0x%llx\n", get_pud_base(pdg_id));
    printf("-> PMD base is 0x%llx\n", get_pmd_base(pud_id));
    printf("-> Block address is 0x%llx\n", block_addr);
    printf("-> Block offset is 0x%llx\n", block_offs);


    return (block_addr + block_offs);
}

int get_memory_type_index(ULONG va){
    unsigned char* type[] = {
        "DEVICE",
        "NORMAL"
    };
    ULONG pmd_id = GET_PMD_ID(va);
    ULONG pmd_ent = pmd[pmd_id];
    int memory_type = (pmd_id >> 2) & 8;
    return memory_type;
}

int main(){
    printf("OPENING FILE...\n");
    f = fopen("tools/workmmu.bin","rb");  // r for read, b for binary

    if (!f) {
        printf("[ERROR] File vmm.bin not found");
        return -1;
    }

    printf("size of PGD: %d\n", sizeof(pgd));

    fread(pgd,sizeof(pgd),1,f);
    fread(pud,sizeof(pud),1,f);
    fread(pmd,sizeof(pmd),1,f);

    mem_offset = (pgd[0] & VA_MASK) - sizeof(pgd);
    printf("Memory different from value to file offset is 0x%X\n", mem_offset);

    printf("first long of PGD %x\n", pgd[0]);
    printf("last long of PUD %x\n", pud[511]);
    printf("first long of PMD %x\n", pmd[0]);

    ULONG va = 0xFFFFFFFFFF000000;

    // all verified
    printf("-------- [0x%llX] --------\n", va);
    printf("Physical address of 0x%llx is 0x%llx (type: %d)\n", va, get_physical_addr(va), get_memory_type_index(va));

    // debug
    printf("----------- DUMP BLOCK MAP ---------------\n");
    for (int i=0; i < 10; i++){
        printf("PMD index %d has value of 0x%llx\n", i, (pmd[i] & 0xFFFFFFFFFFE00000));
    }
    return 0;
}

