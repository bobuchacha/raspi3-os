/*
 * ldr_format.h
 *
 * Custom container format consumed by the MVP loader.
 *
 * This format is intentionally simple and deterministic so the
 * Python builder can produce `.exe`, `.dll`, and `.sys` artifacts.
 */
#ifndef LDR_FORMAT_H
#define LDR_FORMAT_H

#include "ldr_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define LDR_IMAGE_MAGIC 0x3044524cU /* "LRD0" little-endian */
#define LDR_IMAGE_VERSION 1U

    /* Target artifact kind encoded in the image header. */
    typedef enum LdrImageKindEnum
    {
        LDR_IMAGE_EXE = 1,
        LDR_IMAGE_DLL = 2,
        LDR_IMAGE_SYS = 3
    } LDR_IMAGEKIND;

    /* Section flags mapped to VM permissions. */
    enum
    {
        LDR_SEC_READ = 1u << 0,
        LDR_SEC_WRITE = 1u << 1,
        LDR_SEC_EXEC = 1u << 2,
        LDR_SEC_BSS = 1u << 3
    };

    /* Relocation types supported in MVP format. */
    enum
    {
        LDR_RELOC_ABS64 = 1
    };

#pragma pack(push, 1)

    /* Fixed-size header at file offset 0. */
    typedef struct LdrImageHeaderStruct
    {
        UInt magic;
        UWord version;
        UWord kind;
        UInt flags;
        ULong imageSize;
        ULong preferredBase;
        ULong entryRva;
        UInt sectionCount;
        UInt relocCount;
        UInt importCount;
        UInt exportCount;
        UInt stringTableSize;
        UInt reserved0;
    } LDR_IMAGEHEADER;

    /* Section metadata table entry. */
    typedef struct LdrSectionDescStruct
    {
        UInt nameOffset;
        UInt flags;
        ULong rva;
        ULong fileOffset;
        ULong fileSize;
        ULong virtualSize;
    } LDR_SECTIONDESC;

    /* Relocation metadata table entry. */
    typedef struct LdrRelocDescStruct
    {
        ULong patchRva;
        UInt type;
        UInt reserved0;
        Long addend;
    } LDR_RELOCDESC;

    /* Import metadata table entry. */
    typedef struct LdrImportDescStruct
    {
        UInt moduleNameOffset;
        UInt symbolNameOffset;
        ULong iatRva;
    } LDR_IMPORTDESC;

    /* Export metadata table entry. */
    typedef struct LdrExportDescStruct
    {
        UInt symbolNameOffset;
        UInt reserved0;
        ULong symbolRva;
    } LDR_EXPORTDESC;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* LDR_FORMAT_H */
