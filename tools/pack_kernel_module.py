#!/usr/bin/env python3

import argparse
import json
import pathlib
import struct
from collections import OrderedDict
from typing import Dict, List, Optional, Tuple


ELF_MAGIC = b"\x7fELF"
ELF_CLASS_64 = 2
ELF_DATA_LSB = 1
ELF_MACHINE_AARCH64 = 183
ELF_TYPE_EXEC = 2
ELF_TYPE_DYN = 3
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_NOBITS = 8
SHT_DYNSYM = 11
SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHN_UNDEF = 0
SHN_ABS = 0xFFF1
R_AARCH64_ABS64 = 257
R_AARCH64_PREL64 = 260
R_AARCH64_GLOB_DAT = 1025
R_AARCH64_JUMP_SLOT = 1026
R_AARCH64_RELATIVE = 1027

SECTION_HEADER_FORMAT = struct.Struct("<IIQQQQIIQQ")
SYMBOL_FORMAT = struct.Struct("<IBBHQQ")
RELA_FORMAT = struct.Struct("<QQq")
ELF_HEADER_FORMAT = struct.Struct("<16sHHIQQQIHHHHHH")

MODULE_MAGIC = b"ROSKMOD\x00"
MODULE_VERSION = 1
MODULE_HEADER_SIZE = 256
MODULE_PAYLOAD_ALIGN = 16
MODULE_NAME_SIZE = 64
MODULE_EXPORT_NAME_SIZE = 16
MODULE_MAX_EXPORTS = 4
MODULE_INVALID_SECTION = 0xFFFFFFFF
MODULE_HEADER_FORMAT = struct.Struct("<8sIHHIIII64sIIIIQQQII16sIQ16sIQ16sIQ16sIQ")

FLAT_MAGIC = b"ROSMOD\x00\x00"
FLAT_ABI_VERSION = 1
FLAT_HEADER_FORMAT = struct.Struct("<8sHHIIIIIIIQQQQ")
FLAT_SECTION_FORMAT = struct.Struct("<IIQQQQQ")
FLAT_IMPORT_FORMAT = struct.Struct("<IIQ")
FLAT_RELOC_FORMAT = struct.Struct("<IIQQ")

MOD_SEC_TEXT = 0
MOD_SEC_RODATA = 1
MOD_SEC_DATA = 2
MOD_RELOC_ABS64 = 0
MOD_RELOC_REL64 = 1
MOD_RELOC_SECTION = 2
MOD_IMPORT_ABS64 = 0
MOD_IMPORT_REL64 = 1
MOD_SECTION_FLAG_READ = 0x1
MOD_SECTION_FLAG_WRITE = 0x2
MOD_SECTION_FLAG_EXEC = 0x4
PAGE_SIZE = 0x1000


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Bundle a kernel module manifest and ELF image into a .sys file")
    parser.add_argument("--input", required=True, help="Input ELF shared object")
    parser.add_argument("--manifest", help="Optional module.json fallback path")
    parser.add_argument("--output", required=True, help="Output .sys path")
    return parser.parse_args()


EMBEDDED_METADATA_SECTION = ".ros.module.meta"
EMBEDDED_METADATA_MAGIC = b"ROSKMETA"
EMBEDDED_METADATA_VERSION = 1
EMBEDDED_METADATA_HEADER_FORMAT = struct.Struct("<8sII64s64s64s64sI")
EMBEDDED_METADATA_EXPORT_FORMAT = struct.Struct("<16s64s")


def encode_fixed_ascii(text: str, size: int, field_name: str) -> bytes:
    encoded = text.encode("ascii")
    if len(encoded) >= size:
        raise ValueError(f"{field_name} is too long: {text}")
    return encoded + (b"\x00" * (size - len(encoded)))


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def read_c_string(blob: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(blob):
        return ""
    end = blob.find(b"\x00", offset)
    if end < 0:
        end = len(blob)
    return blob[offset:end].decode("ascii", errors="ignore")


def decode_fixed_ascii(blob: bytes) -> str:
    return blob.split(b"\x00", 1)[0].decode("ascii", errors="ignore")


def classify_section(flags: int) -> Optional[int]:
    if (flags & SHF_ALLOC) == 0:
        return None
    if flags & SHF_EXECINSTR:
        return MOD_SEC_TEXT
    if flags & SHF_WRITE:
        return MOD_SEC_DATA
    return MOD_SEC_RODATA


def validate_manifest_dict(manifest: dict) -> dict:
    name = manifest.get("name")
    init_symbol = manifest.get("init")
    shutdown_symbol = manifest.get("shutdown", "")
    idle_symbol = manifest.get("idle", "")
    exports = manifest.get("exports", [])
    if not isinstance(name, str) or not name:
        raise ValueError("manifest requires a non-empty 'name'")
    if not isinstance(init_symbol, str) or not init_symbol:
        raise ValueError("manifest requires a non-empty 'init'")
    if not isinstance(shutdown_symbol, str):
        raise ValueError("manifest field 'shutdown' must be a string when provided")
    if not isinstance(idle_symbol, str):
        raise ValueError("manifest field 'idle' must be a string when provided")
    if not isinstance(exports, list):
        raise ValueError("manifest field 'exports' must be an array when provided")
    if len(exports) > MODULE_MAX_EXPORTS:
        raise ValueError(f"manifest supports at most {MODULE_MAX_EXPORTS} exports")
    for export in exports:
        if not isinstance(export, dict):
            raise ValueError("each export entry must be an object")
        export_name = export.get("name")
        export_symbol = export.get("symbol")
        if not isinstance(export_name, str) or not export_name:
            raise ValueError("each export requires a non-empty 'name'")
        if not isinstance(export_symbol, str) or not export_symbol:
            raise ValueError("each export requires a non-empty 'symbol'")
        encode_fixed_ascii(export_name, MODULE_EXPORT_NAME_SIZE, "export name")

    manifest.setdefault("abi_version", MODULE_VERSION)
    return manifest


def encode_manifest(manifest: dict) -> bytes:
    return (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")


def load_manifest(path: pathlib.Path) -> Tuple[dict, bytes]:
    manifest = validate_manifest_dict(json.loads(path.read_text()))
    return manifest, encode_manifest(manifest)


def find_section_by_name(data: bytes, name: str) -> Optional[Tuple[int, ...]]:
    sections = load_section_headers(data)
    shstrtab = load_section_name_table(data, sections)

    for section in sections:
        sh_name = section[0]
        if read_c_string(shstrtab, sh_name) == name:
            return section

    return None


def load_embedded_manifest(data: bytes) -> Optional[Tuple[dict, bytes]]:
    section = find_section_by_name(data, EMBEDDED_METADATA_SECTION)
    if section is None:
        return None

    sh_offset = section[4]
    sh_size = section[5]
    expected_size = EMBEDDED_METADATA_HEADER_FORMAT.size + (MODULE_MAX_EXPORTS * EMBEDDED_METADATA_EXPORT_FORMAT.size)

    if sh_size < expected_size:
        raise ValueError("embedded module metadata section is truncated")
    if sh_offset + expected_size > len(data):
        raise ValueError("embedded module metadata points past end of file")

    blob = data[sh_offset : sh_offset + expected_size]
    magic, version, flags, raw_name, raw_init, raw_shutdown, raw_idle, export_count = EMBEDDED_METADATA_HEADER_FORMAT.unpack_from(blob, 0)

    if magic != EMBEDDED_METADATA_MAGIC:
        raise ValueError("embedded module metadata magic is invalid")
    if version != EMBEDDED_METADATA_VERSION:
        raise ValueError(f"unsupported embedded module metadata version: {version}")
    if export_count > MODULE_MAX_EXPORTS:
        raise ValueError(f"embedded module metadata supports at most {MODULE_MAX_EXPORTS} exports")

    exports = []
    export_offset = EMBEDDED_METADATA_HEADER_FORMAT.size
    for index in range(MODULE_MAX_EXPORTS):
        raw_export_name, raw_export_symbol = EMBEDDED_METADATA_EXPORT_FORMAT.unpack_from(blob, export_offset)
        export_offset += EMBEDDED_METADATA_EXPORT_FORMAT.size
        if index >= export_count:
            continue

        export_name = decode_fixed_ascii(raw_export_name)
        export_symbol = decode_fixed_ascii(raw_export_symbol)
        if not export_name or not export_symbol:
            raise ValueError("embedded module export entries must have non-empty names and symbols")
        exports.append({"name": export_name, "symbol": export_symbol})

    manifest = validate_manifest_dict(
        {
            "name": decode_fixed_ascii(raw_name),
            "init": decode_fixed_ascii(raw_init),
            "shutdown": decode_fixed_ascii(raw_shutdown),
            "idle": decode_fixed_ascii(raw_idle),
            "exports": exports,
            "flags": flags,
        }
    )
    return manifest, encode_manifest(manifest)


def load_section_headers(data: bytes) -> List[Tuple[int, ...]]:
    header = ELF_HEADER_FORMAT.unpack_from(data, 0)
    e_shoff = header[6]
    e_shentsize = header[11]
    e_shnum = header[12]

    if e_shentsize != SECTION_HEADER_FORMAT.size:
        raise ValueError("unexpected ELF section header size")

    sections: List[Tuple[int, ...]] = []
    for index in range(e_shnum):
        offset = e_shoff + (index * e_shentsize)
        if offset + SECTION_HEADER_FORMAT.size > len(data):
            raise ValueError("ELF section header table is truncated")
        sections.append(SECTION_HEADER_FORMAT.unpack_from(data, offset))
    return sections


def load_section_name_table(data: bytes, sections: List[Tuple[int, ...]]) -> bytes:
    header = ELF_HEADER_FORMAT.unpack_from(data, 0)
    shstr_index = header[13]

    if shstr_index >= len(sections):
        raise ValueError("ELF section name string table is invalid")

    shstr = sections[shstr_index]
    offset = shstr[4]
    size = shstr[5]
    if offset + size > len(data):
        raise ValueError("ELF section name string table is truncated")
    return data[offset : offset + size]


def load_elf_symbols(data: bytes) -> Tuple[List[dict], Dict[str, dict]]:
    sections = load_section_headers(data)
    symbols: List[dict] = []
    by_name: Dict[str, dict] = {}

    for section in sections:
        sh_type = section[1]
        sh_offset = section[4]
        sh_size = section[5]
        sh_link = section[6]
        sh_entsize = section[9]

        if sh_type not in (SHT_SYMTAB, SHT_DYNSYM) or sh_entsize != SYMBOL_FORMAT.size:
            continue
        if sh_link >= len(sections):
            raise ValueError("ELF symbol table string section is invalid")

        strtab = sections[sh_link]
        strtab_offset = strtab[4]
        strtab_size = strtab[5]
        if strtab_offset + strtab_size > len(data):
            raise ValueError("ELF string table is truncated")
        strings = data[strtab_offset : strtab_offset + strtab_size]

        count = sh_size // sh_entsize
        for index in range(count):
            sym_offset = sh_offset + (index * sh_entsize)
            if sym_offset + SYMBOL_FORMAT.size > len(data):
                raise ValueError("ELF symbol table is truncated")
            st_name, st_info, st_other, st_shndx, st_value, st_size = SYMBOL_FORMAT.unpack_from(data, sym_offset)
            name = read_c_string(strings, st_name)
            symbol = {
                "name": name,
                "info": st_info,
                "other": st_other,
                "shndx": st_shndx,
                "value": st_value,
                "size": st_size,
            }
            symbols.append(symbol)
            if name:
                by_name[name] = symbol

    return symbols, by_name


def validate_elf(elf_path: pathlib.Path, required_symbols: List[str]) -> Tuple[bytes, Dict[str, dict]]:
    data = elf_path.read_bytes()
    if len(data) < ELF_HEADER_FORMAT.size:
        raise ValueError("ELF file is too small")

    header = ELF_HEADER_FORMAT.unpack_from(data, 0)
    e_ident = header[0]
    e_type = header[1]
    e_machine = header[2]

    if e_ident[:4] != ELF_MAGIC:
        raise ValueError("input is not an ELF file")
    if e_ident[4] != ELF_CLASS_64:
        raise ValueError("expected ELF64 input")
    if e_ident[5] != ELF_DATA_LSB:
        raise ValueError("expected little-endian ELF input")
    if e_type not in (ELF_TYPE_EXEC, ELF_TYPE_DYN):
        raise ValueError("expected ET_EXEC or ET_DYN input")
    if e_machine != ELF_MACHINE_AARCH64:
        raise ValueError("expected AArch64 ELF input")

    _symbols, by_name = load_elf_symbols(data)
    for symbol in required_symbols:
        if symbol and symbol not in by_name:
            raise ValueError(f"required symbol not found in ELF image: {symbol}")

    return data, by_name


def build_alloc_sections(elf_data: bytes) -> Tuple[List[Tuple[int, ...]], List[dict]]:
    sections = load_section_headers(elf_data)
    shstrtab = load_section_name_table(elf_data, sections)
    alloc_sections: List[dict] = []

    for index, section in enumerate(sections):
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, _sh_link, _sh_info, sh_addralign, _sh_entsize = section
        logical_type = classify_section(sh_flags)
        if logical_type is None:
            continue

        raw = b""
        if sh_type != SHT_NOBITS:
            if sh_offset + sh_size > len(elf_data):
                raise ValueError(f"alloc section {index} points past end of file")
            raw = elf_data[sh_offset : sh_offset + sh_size]

        alloc_sections.append(
            {
                "index": index,
                "name": read_c_string(shstrtab, sh_name),
                "type": sh_type,
                "flags": sh_flags,
                "addr": sh_addr,
                "size": sh_size,
                "align": max(1, sh_addralign),
                "data": raw,
                "logical_type": logical_type,
            }
        )

    alloc_sections.sort(key=lambda entry: (entry["addr"], entry["index"]))
    return sections, alloc_sections


def build_flat_sections(alloc_sections: List[dict]) -> List[dict]:
    logical_sections: List[dict] = []

    for member in alloc_sections:
        logical_type = member["logical_type"]
        payload = bytearray(member["data"]) if member["type"] != SHT_NOBITS and member["size"] else bytearray()

        member["logical_section_index"] = len(logical_sections)
        member["logical_offset"] = 0
        logical_sections.append(
            {
                "type": logical_type,
                "flags": MOD_SECTION_FLAG_READ | (MOD_SECTION_FLAG_EXEC if logical_type == MOD_SEC_TEXT else 0) | (MOD_SECTION_FLAG_WRITE if logical_type == MOD_SEC_DATA else 0),
                "align": max(PAGE_SIZE, member["align"]),
                "file_size": len(payload),
                "mem_size": member["size"],
                "payload": payload,
                "runtime_offset": member["addr"],
            }
        )

    return logical_sections


def map_image_address(alloc_sections: List[dict], address: int) -> Tuple[int, int]:
    for section in alloc_sections:
        start = section["addr"]
        end = start + section["size"]
        if start <= address < end:
            return section["logical_section_index"], section["logical_offset"] + (address - start)
    raise ValueError(f"address 0x{address:x} does not resolve to an allocated module section")


def resolve_symbol_reference(symbol: dict, addend: int, alloc_sections: List[dict]) -> Optional[Tuple[int, int]]:
    if symbol["shndx"] == SHN_UNDEF:
        return None
    if symbol["shndx"] == SHN_ABS:
        raise ValueError(f"absolute symbol is not supported in flat module output: {symbol['name']}")

    section_index, section_offset = map_image_address(alloc_sections, symbol["value"])
    return section_index, section_offset + addend


def overwrite_u64(payload: bytearray, offset: int, value: int) -> None:
    end = offset + 8
    if len(payload) < end:
        payload.extend(b"\x00" * (end - len(payload)))
    payload[offset:end] = struct.pack("<Q", value & 0xFFFFFFFFFFFFFFFF)


def build_relocations(elf_data: bytes, sections: List[Tuple[int, ...]], alloc_sections: List[dict], logical_sections: List[dict]) -> Tuple[List[dict], List[dict], List[str]]:
    symbols, _symbols_by_name = load_elf_symbols(elf_data)
    imports: List[dict] = []
    relocs: List[dict] = []
    import_names: OrderedDict[str, None] = OrderedDict()

    for section in sections:
        sh_type = section[1]
        sh_offset = section[4]
        sh_size = section[5]
        sh_link = section[6]
        sh_entsize = section[9]

        if sh_type != SHT_RELA:
            continue
        if sh_entsize != RELA_FORMAT.size:
            raise ValueError("unexpected ELF rela entry size")
        if sh_link >= len(sections):
            raise ValueError("ELF relocation symbol table link is invalid")
        if sh_offset + sh_size > len(elf_data):
            raise ValueError("ELF relocation table is truncated")

        count = sh_size // sh_entsize
        for index in range(count):
            rela_offset = sh_offset + (index * sh_entsize)
            r_offset, r_info, r_addend = RELA_FORMAT.unpack_from(elf_data, rela_offset)
            r_type = r_info & 0xFFFFFFFF
            r_sym = r_info >> 32

            if r_type == 0:
                continue

            patch_section, patch_offset = map_image_address(alloc_sections, r_offset)
            patch_image_offset = logical_sections[patch_section]["runtime_offset"] + patch_offset
            patch_payload = logical_sections[patch_section]["payload"]

            if r_sym >= len(symbols) and r_type != R_AARCH64_RELATIVE:
                raise ValueError(f"relocation references invalid symbol index {r_sym}")

            symbol = symbols[r_sym] if r_sym < len(symbols) else None
            symbol_name = symbol["name"] if symbol else ""

            if r_type == R_AARCH64_RELATIVE:
                target_section, target_offset = map_image_address(alloc_sections, r_addend)
                relocs.append(
                    {
                        "type": MOD_RELOC_SECTION,
                        "section": target_section,
                        "offset": patch_image_offset,
                        "addend": target_offset,
                    }
                )
                continue

            if r_type in (R_AARCH64_ABS64, R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT):
                if symbol is None:
                    raise ValueError(f"relocation {r_type} references an invalid symbol index {r_sym}")
                if symbol["shndx"] == SHN_UNDEF:
                    if not symbol_name:
                        raise ValueError("flat module import has no symbol name")
                    import_names.setdefault(symbol_name, None)
                    overwrite_u64(patch_payload, patch_offset, r_addend)
                    imports.append(
                        {
                            "name": symbol_name,
                            "type": MOD_IMPORT_ABS64,
                            "patch_offset": patch_image_offset,
                        }
                    )
                else:
                    target = resolve_symbol_reference(symbol, r_addend, alloc_sections)
                    if target is None:
                        raise ValueError(f"unable to resolve relocation target for symbol {symbol_name}")
                    relocs.append(
                        {
                            "type": MOD_RELOC_SECTION,
                            "section": target[0],
                            "offset": patch_image_offset,
                            "addend": target[1],
                        }
                    )
                continue

            if r_type == R_AARCH64_PREL64:
                if symbol is None:
                    raise ValueError(f"relocation {r_type} references an invalid symbol index {r_sym}")
                if symbol["shndx"] == SHN_UNDEF:
                    if not symbol_name:
                        raise ValueError("flat module relative import has no symbol name")
                    import_names.setdefault(symbol_name, None)
                    overwrite_u64(patch_payload, patch_offset, r_addend)
                    imports.append(
                        {
                            "name": symbol_name,
                            "type": MOD_IMPORT_REL64,
                            "patch_offset": patch_image_offset,
                        }
                    )
                else:
                    target = resolve_symbol_reference(symbol, r_addend, alloc_sections)
                    if target is None:
                        raise ValueError(f"unable to resolve relocation target for symbol {symbol_name}")
                    relocs.append(
                        {
                            "type": MOD_RELOC_REL64,
                            "section": target[0],
                            "offset": patch_image_offset,
                            "addend": target[1],
                        }
                    )
                continue

            raise ValueError(f"unsupported AArch64 relocation type: {r_type}")

    return imports, relocs, list(import_names.keys())


def resolve_bundle_symbol(symbol_name: str, by_name: Dict[str, dict], alloc_sections: List[dict]) -> Tuple[int, int]:
    if not symbol_name:
        return MODULE_INVALID_SECTION, 0
    if symbol_name not in by_name:
        raise ValueError(f"required symbol not found in ELF image: {symbol_name}")

    symbol = by_name[symbol_name]
    target = resolve_symbol_reference(symbol, 0, alloc_sections)
    if target is None:
        raise ValueError(f"required symbol is undefined in ELF image: {symbol_name}")
    return target


def build_flat_module(manifest: dict, elf_data: bytes, by_name: Dict[str, dict]) -> Tuple[bytes, Dict[str, int]]:
    sections, alloc_sections = build_alloc_sections(elf_data)
    logical_sections = build_flat_sections(alloc_sections)
    imports, relocs, import_names = build_relocations(elf_data, sections, alloc_sections, logical_sections)

    string_blob = bytearray()
    relative_name_offsets: Dict[str, int] = {}
    for name in import_names:
        relative_name_offsets[name] = len(string_blob)
        string_blob.extend(name.encode("ascii"))
        string_blob.append(0)

    metadata_size = (
        FLAT_HEADER_FORMAT.size
        + (len(logical_sections) * FLAT_SECTION_FORMAT.size)
        + (len(imports) * FLAT_IMPORT_FORMAT.size)
        + (len(relocs) * FLAT_RELOC_FORMAT.size)
        + len(string_blob)
    )
    metadata_size = align_up(metadata_size, MODULE_PAYLOAD_ALIGN)

    payload_cursor = metadata_size
    section_entries = bytearray()
    for logical_section in logical_sections:
        section_entries.extend(
            FLAT_SECTION_FORMAT.pack(
                logical_section["type"],
                logical_section["flags"],
                logical_section["runtime_offset"],
                payload_cursor,
                logical_section["file_size"],
                logical_section["mem_size"],
                logical_section["align"],
            )
        )
        payload_cursor += logical_section["file_size"]

    strings_base = (
        FLAT_HEADER_FORMAT.size
        + (len(logical_sections) * FLAT_SECTION_FORMAT.size)
        + (len(imports) * FLAT_IMPORT_FORMAT.size)
        + (len(relocs) * FLAT_RELOC_FORMAT.size)
    )

    import_entries = bytearray()
    for entry in imports:
        import_entries.extend(
            FLAT_IMPORT_FORMAT.pack(
                strings_base + relative_name_offsets[entry["name"]],
                entry["type"],
                entry["patch_offset"],
            )
        )

    reloc_entries = bytearray()
    for entry in relocs:
        reloc_entries.extend(
            FLAT_RELOC_FORMAT.pack(
                entry["type"],
                entry["section"],
                entry["offset"],
                entry["addend"],
            )
        )

    image_size = 0
    bss_size = 0
    for logical_section in logical_sections:
        image_size = max(image_size, logical_section["runtime_offset"] + logical_section["mem_size"])
        bss_size += logical_section["mem_size"] - logical_section["file_size"]
    image_size = align_up(image_size, PAGE_SIZE)

    init_section, init_offset = resolve_bundle_symbol(manifest["init"], by_name, alloc_sections)
    shutdown_section, shutdown_offset = resolve_bundle_symbol(manifest.get("shutdown", ""), by_name, alloc_sections)
    idle_section, idle_offset = resolve_bundle_symbol(manifest.get("idle", ""), by_name, alloc_sections)
    export_entries = []

    for export in manifest.get("exports", []):
        export_section, export_offset = resolve_bundle_symbol(export["symbol"], by_name, alloc_sections)
        export_entries.append(
            {
                "name": export["name"],
                "section": export_section,
                "offset": export_offset,
            }
        )

    flat_header = bytearray(
        FLAT_HEADER_FORMAT.pack(
            FLAT_MAGIC,
            FLAT_ABI_VERSION,
            ELF_MACHINE_AARCH64,
            int(manifest.get("flags", 0)),
            metadata_size,
            len(logical_sections),
            len(imports),
            len(relocs),
            init_section,
            0,
            init_offset,
            image_size,
            bss_size,
            PAGE_SIZE,
        )
    )
    module_payload = bytearray(flat_header)
    module_payload.extend(section_entries)
    module_payload.extend(import_entries)
    module_payload.extend(reloc_entries)
    module_payload.extend(string_blob)
    module_payload.extend(b"\x00" * (metadata_size - len(module_payload)))

    for logical_section in logical_sections:
        module_payload.extend(logical_section["payload"][: logical_section["file_size"]])

    bundle_meta = {
        "init_section": init_section,
        "init_offset": init_offset,
        "shutdown_section": shutdown_section,
        "shutdown_offset": shutdown_offset,
        "idle_section": idle_section,
        "idle_offset": idle_offset,
        "exports": export_entries,
    }
    return bytes(module_payload), bundle_meta


def encode_bundle_exports(exports: List[Dict[str, int]]) -> List[object]:
    encoded: List[object] = []

    for export in exports:
        encoded.extend(
            [
                encode_fixed_ascii(export["name"], MODULE_EXPORT_NAME_SIZE, "export name"),
                export["section"],
                export["offset"],
            ]
        )

    for _ in range(MODULE_MAX_EXPORTS - len(exports)):
        encoded.extend([b"\x00" * MODULE_EXPORT_NAME_SIZE, MODULE_INVALID_SECTION, 0])

    return encoded


def build_bundle(manifest: dict, manifest_bytes: bytes, module_bytes: bytes, bundle_meta: Dict[str, int]) -> bytes:
    manifest_offset = MODULE_HEADER_SIZE
    module_offset = align_up(manifest_offset + len(manifest_bytes), MODULE_PAYLOAD_ALIGN)
    padding = b"\x00" * (module_offset - (manifest_offset + len(manifest_bytes)))

    header = bytearray(
        MODULE_HEADER_FORMAT.pack(
            MODULE_MAGIC,
            MODULE_HEADER_SIZE,
            MODULE_VERSION,
            int(manifest.get("abi_version", MODULE_VERSION)),
            int(manifest.get("flags", 0)),
            len(manifest_bytes),
            len(module_bytes),
            MODULE_PAYLOAD_ALIGN,
            encode_fixed_ascii(manifest["name"], MODULE_NAME_SIZE, "module name"),
            bundle_meta["init_section"],
            bundle_meta["shutdown_section"],
            bundle_meta["idle_section"],
            0,
            bundle_meta["init_offset"],
            bundle_meta["shutdown_offset"],
            bundle_meta["idle_offset"],
            len(bundle_meta["exports"]),
            0,
            *encode_bundle_exports(bundle_meta["exports"]),
        )
    )
    header.extend(b"\x00" * (MODULE_HEADER_SIZE - len(header)))
    return bytes(header) + manifest_bytes + padding + module_bytes


def main() -> int:
    args = parse_args()
    elf_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)

    elf_bytes = elf_path.read_bytes()
    embedded_manifest = load_embedded_manifest(elf_bytes)
    if embedded_manifest is not None:
        manifest, manifest_bytes = embedded_manifest
    elif args.manifest:
        manifest, manifest_bytes = load_manifest(pathlib.Path(args.manifest))
    else:
        raise ValueError("module packer requires either embedded metadata or --manifest")

    elf_data, by_name = validate_elf(
        elf_path,
        [
            manifest["init"],
            manifest.get("shutdown", ""),
            manifest.get("idle", ""),
            *[export["symbol"] for export in manifest.get("exports", [])],
        ],
    )
    module_bytes, bundle_meta = build_flat_module(manifest, elf_data, by_name)
    bundle = build_bundle(manifest, manifest_bytes, module_bytes, bundle_meta)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(bundle)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
