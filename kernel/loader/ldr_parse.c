/*
 * ldr_parse.c
 *
 * Parse custom image container into loader metadata structures.
 */
#include "../include/ldr_internal.h"

#include <string.h>

/*
 * ldr_strdup_ctx
 *
 * Duplicate string using loader heap allocator.
 *
 * Args:
 *   ctx - loader context.
 *   src - source C string.
 *
 * Returns:
 *   Heap-allocated string copy, or NULL on allocation failure.
 */
static char *
ldr_strdup_ctx(PLDR_CONTEXT context, CONST char *sourceText) {
    Size  textLength;
    char *copyText;

    if (!context || !sourceText) {
        return NULL;
    }

    textLength = (Size)strlen(sourceText);
    copyText = (char *)context->api->heapAlloc(textLength + 1);
    if (!copyText) {
        return NULL;
    }
    memcpy(copyText, sourceText, (size_t)(textLength + 1));
    return copyText;
}

/*
 * ldr_str_at
 *
 * Resolve string pointer from string table by offset.
 *
 * Args:
 *   strtab - base pointer of string table.
 *   strtab_size - total string table bytes.
 *   off - offset inside string table.
 *
 * Returns:
 *   Valid C string pointer on success, NULL on bounds error.
 */
static CONST char *
ldr_str_at(CONST UByte *stringTable, UInt stringTableSize, UInt stringOffset) {
    UInt stringIndex;

    if (!stringTable || stringOffset >= stringTableSize) {
        return NULL;
    }

    /* Ensure the string is NUL-terminated before table end. */
    for (stringIndex = stringOffset; stringIndex < stringTableSize; ++stringIndex) {
        if (stringTable[stringIndex] == '\0') {
            return (CONST char *)(stringTable + stringOffset);
        }
    }
    return NULL;
}

/*
 * ldr_parse_image
 *
 * Parse one image file in memory and allocate module metadata arrays.
 *
 * Args:
 *   ctx - loader context with heap allocator.
 *   buffer - complete file bytes.
 *   size - file size in bytes.
 *   path - source path copied to module metadata.
 *   out_module - receives parsed module object.
 *
 * Returns:
 *   LDR_OK on success, format/oom/invalid errors otherwise.
 */
LDR_RESULT
ldr_parse_image(PLDR_CONTEXT context, CONST UByte *buffer, Size size, CONST char *path, PLDR_MODULE *outModule) {
    CONST LDR_IMAGEHEADER *header;
    CONST LDR_SECTIONDESC *sectionDesc;
    CONST LDR_RELOCDESC   *relocDesc;
    CONST LDR_IMPORTDESC  *importDesc;
    CONST LDR_EXPORTDESC  *exportDesc;
    CONST UByte           *stringTable;
    UInt                   itemIndex;
    Size                   cursor;
    PLDR_MODULE            module;

    if (!context || !buffer || !outModule || size < (Size)sizeof(LDR_IMAGEHEADER)) {
        return LDR_E_INVALID_ARG;
    }

    header = (CONST LDR_IMAGEHEADER *)buffer;
    if (header->magic != LDR_IMAGE_MAGIC || header->version != LDR_IMAGE_VERSION) {
        return LDR_E_FORMAT;
    }

    cursor = (Size)sizeof(*header);

    /* Validate and locate all metadata tables in strict layout order. */
    if (cursor + ((Size)header->sectionCount * (Size)sizeof(*sectionDesc)) > size) {
        return LDR_E_FORMAT;
    }
    sectionDesc = (CONST LDR_SECTIONDESC *)(buffer + cursor);
    cursor += (Size)header->sectionCount * (Size)sizeof(*sectionDesc);

    if (cursor + ((Size)header->relocCount * (Size)sizeof(*relocDesc)) > size) {
        return LDR_E_FORMAT;
    }
    relocDesc = (CONST LDR_RELOCDESC *)(buffer + cursor);
    cursor += (Size)header->relocCount * (Size)sizeof(*relocDesc);

    if (cursor + ((Size)header->importCount * (Size)sizeof(*importDesc)) > size) {
        return LDR_E_FORMAT;
    }
    importDesc = (CONST LDR_IMPORTDESC *)(buffer + cursor);
    cursor += (Size)header->importCount * (Size)sizeof(*importDesc);

    if (cursor + ((Size)header->exportCount * (Size)sizeof(*exportDesc)) > size) {
        return LDR_E_FORMAT;
    }
    exportDesc = (CONST LDR_EXPORTDESC *)(buffer + cursor);
    cursor += (Size)header->exportCount * (Size)sizeof(*exportDesc);

    if (cursor + header->stringTableSize > size) {
        return LDR_E_FORMAT;
    }
    stringTable = buffer + cursor;

    /* Allocate and initialize module object before copying metadata arrays. */
    module = (PLDR_MODULE)context->api->heapAlloc((Size)sizeof(*module));
    if (!module) {
        return LDR_E_OOM;
    }
    memset(module, 0, sizeof(*module));

    module->kind = (LDR_IMAGEKIND)header->kind;
    module->imageSize = header->imageSize;
    module->preferredBase = header->preferredBase;
    module->entryRva = header->entryRva;
    module->sectionCount = header->sectionCount;
    module->relocCount = header->relocCount;
    module->importCount = header->importCount;
    module->exportCount = header->exportCount;
    module->path = ldr_strdup_ctx(context, path ? path : "<memory>");
    module->referenceCount = 1;

    if (module->sectionCount) {
        module->sections = (PLDR_SECTION)context->api->heapAlloc((Size)sizeof(LDR_SECTION) * module->sectionCount);
        if (!module->sections) {
            return LDR_E_OOM;
        }
        memset(module->sections, 0, (size_t)(sizeof(LDR_SECTION) * module->sectionCount));

        for (itemIndex = 0; itemIndex < module->sectionCount; ++itemIndex) {
            CONST char *sectionName = ldr_str_at(stringTable, header->stringTableSize, sectionDesc[itemIndex].nameOffset);
            if (!sectionName) {
                return LDR_E_FORMAT;
            }
            module->sections[itemIndex].name = ldr_strdup_ctx(context, sectionName);
            module->sections[itemIndex].flags = sectionDesc[itemIndex].flags;
            module->sections[itemIndex].rva = sectionDesc[itemIndex].rva;
            module->sections[itemIndex].fileOffset = sectionDesc[itemIndex].fileOffset;
            module->sections[itemIndex].fileSize = sectionDesc[itemIndex].fileSize;
            module->sections[itemIndex].virtualSize = sectionDesc[itemIndex].virtualSize;
        }
    }

    if (module->relocCount) {
        module->relocs = (PLDR_RELOC)context->api->heapAlloc((Size)sizeof(LDR_RELOC) * module->relocCount);
        if (!module->relocs) {
            return LDR_E_OOM;
        }
        for (itemIndex = 0; itemIndex < module->relocCount; ++itemIndex) {
            module->relocs[itemIndex].patchRva = relocDesc[itemIndex].patchRva;
            module->relocs[itemIndex].type = relocDesc[itemIndex].type;
            module->relocs[itemIndex].addend = relocDesc[itemIndex].addend;
        }
    }

    if (module->importCount) {
        module->imports = (PLDR_IMPORT)context->api->heapAlloc((Size)sizeof(LDR_IMPORT) * module->importCount);
        if (!module->imports) {
            return LDR_E_OOM;
        }
        memset(module->imports, 0, (size_t)(sizeof(LDR_IMPORT) * module->importCount));

        for (itemIndex = 0; itemIndex < module->importCount; ++itemIndex) {
            CONST char *moduleName = ldr_str_at(stringTable, header->stringTableSize, importDesc[itemIndex].moduleNameOffset);
            CONST char *symbolName = ldr_str_at(stringTable, header->stringTableSize, importDesc[itemIndex].symbolNameOffset);
            if (!moduleName || !symbolName) {
                return LDR_E_FORMAT;
            }
            module->imports[itemIndex].moduleName = ldr_strdup_ctx(context, moduleName);
            module->imports[itemIndex].symbolName = ldr_strdup_ctx(context, symbolName);
            module->imports[itemIndex].iatRva = importDesc[itemIndex].iatRva;
        }
    }

    if (module->exportCount) {
        module->exports = (PLDR_EXPORT)context->api->heapAlloc((Size)sizeof(LDR_EXPORT) * module->exportCount);
        if (!module->exports) {
            return LDR_E_OOM;
        }
        memset(module->exports, 0, (size_t)(sizeof(LDR_EXPORT) * module->exportCount));

        for (itemIndex = 0; itemIndex < module->exportCount; ++itemIndex) {
            CONST char *symbolName = ldr_str_at(stringTable, header->stringTableSize, exportDesc[itemIndex].symbolNameOffset);
            if (!symbolName) {
                return LDR_E_FORMAT;
            }
            module->exports[itemIndex].symbolName = ldr_strdup_ctx(context, symbolName);
            module->exports[itemIndex].symbolRva = exportDesc[itemIndex].symbolRva;
        }
    }

    *outModule = module;
    return LDR_OK;
}
