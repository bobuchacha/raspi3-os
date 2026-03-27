#!/usr/bin/env python3

import argparse
import pathlib
import struct
import sys
from typing import Dict, List, Optional, Tuple


ELF_MAGIC = b"\x7fELF"
ELF_CLASS_64 = 2
ELF_DATA_LSB = 1
ELF_TYPE_DYN = 3
ELF_MACHINE_AARCH64 = 183
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
STB_GLOBAL = 1
STB_WEAK = 2
STT_OBJECT = 1
STT_FUNC = 2

USER_DLL_MAGIC = b"ROSDLL\x00\x00"
USER_DLL_VERSION = 2
USER_DLL_EXPORT_NAME_SIZE = 64
USER_DLL_MAX_EXPORTS = 32
USER_DLL_HEADER_FORMAT = struct.Struct("<8sIIIIIIIIQQQ")
USER_DLL_SECTION_FORMAT = struct.Struct("<IIQQQQQ")
USER_DLL_RELOC_FORMAT = struct.Struct("<IIQQ")
USER_DLL_EXPORT_FORMAT = struct.Struct("<64sIQ")
USER_DLL_PAYLOAD_ALIGN = 16
USER_DLL_ENTRY_SYMBOL = "_shared_library_entry"

USER_DLL_SEC_TEXT = 0
USER_DLL_SEC_RODATA = 1
USER_DLL_SEC_DATA = 2
USER_DLL_RELOC_ABS64 = 0
USER_DLL_RELOC_REL64 = 1
USER_DLL_RELOC_SECTION = 2
USER_DLL_SECTION_FLAG_READ = 0x1
USER_DLL_SECTION_FLAG_WRITE = 0x2
USER_DLL_SECTION_FLAG_EXEC = 0x4
PAGE_SIZE = 0x1000

SECTION_HEADER_FORMAT = struct.Struct("<IIQQQQIIQQ")
SYMBOL_FORMAT = struct.Struct("<IBBHQQ")
RELA_FORMAT = struct.Struct("<QQq")
ELF_HEADER_FORMAT = struct.Struct("<16sHHIQQQIHHHHHH")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Pack a PIC shared ELF into the ROS flat DLL format")
    parser.add_argument("--input", required=True, help="Input shared ELF path")
    parser.add_argument("--output", required=True, help="Output DLL path")
    return parser.parse_args()


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def read_c_string(blob: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(blob):
        return ""
    end = blob.find(b"\x00", offset)
    if end < 0:
        end = len(blob)
    return blob[offset:end].decode("ascii", errors="ignore")


def encode_fixed_ascii(text: str, size: int, field_name: str) -> bytes:
    raw = text.encode("ascii", errors="strict")
    if len(raw) >= size:
        raise ValueError(f"{field_name} too long: {text}")
    return raw + (b"\x00" * (size - len(raw)))


def classify_section(flags: int) -> Optional[int]:
    if (flags & SHF_ALLOC) == 0:
        return None
    if flags & SHF_EXECINSTR:
        return USER_DLL_SEC_TEXT
    if flags & SHF_WRITE:
        return USER_DLL_SEC_DATA
    return USER_DLL_SEC_RODATA


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
    if e_type != ELF_TYPE_DYN:
        raise ValueError("expected ET_DYN input for relocatable DLL output")
    if e_machine != ELF_MACHINE_AARCH64:
        raise ValueError("expected AArch64 ELF input")

    _symbols, by_name = load_elf_symbols(data)
    for symbol in required_symbols:
        if symbol not in by_name:
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
                "flags": USER_DLL_SECTION_FLAG_READ
                | (USER_DLL_SECTION_FLAG_EXEC if logical_type == USER_DLL_SEC_TEXT else 0)
                | (USER_DLL_SECTION_FLAG_WRITE if logical_type == USER_DLL_SEC_DATA else 0),
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
    raise ValueError(f"address 0x{address:x} does not resolve to an allocated DLL section")


def resolve_symbol_reference(symbol: dict, addend: int, alloc_sections: List[dict]) -> Optional[Tuple[int, int]]:
    if symbol["shndx"] == SHN_UNDEF:
        return None
    if symbol["shndx"] == SHN_ABS:
        raise ValueError(f"absolute symbol is not supported in flat DLL output: {symbol['name']}")

    section_index, section_offset = map_image_address(alloc_sections, symbol["value"])
    return section_index, section_offset + addend


def build_relocations(elf_data: bytes, sections: List[Tuple[int, ...]], alloc_sections: List[dict], logical_sections: List[dict]) -> List[dict]:
    symbols, _symbols_by_name = load_elf_symbols(elf_data)
    relocs: List[dict] = []

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

            if r_sym >= len(symbols) and r_type != R_AARCH64_RELATIVE:
                raise ValueError(f"relocation references invalid symbol index {r_sym}")

            symbol = symbols[r_sym] if r_sym < len(symbols) else None

            if r_type == R_AARCH64_RELATIVE:
                target_section, target_offset = map_image_address(alloc_sections, r_addend)
                relocs.append(
                    {
                        "type": USER_DLL_RELOC_SECTION,
                        "section": target_section,
                        "offset": patch_image_offset,
                        "addend": target_offset,
                    }
                )
                continue

            if symbol is None:
                raise ValueError(f"relocation {r_type} references an invalid symbol index {r_sym}")
            if symbol["shndx"] == SHN_UNDEF:
                raise ValueError(f"undefined symbol imports are not supported in flat DLL output: {symbol['name']}")

            target = resolve_symbol_reference(symbol, r_addend, alloc_sections)
            if target is None:
                raise ValueError(f"unable to resolve relocation target for symbol {symbol['name']}")

            if r_type in (R_AARCH64_ABS64, R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT):
                relocs.append(
                    {
                        "type": USER_DLL_RELOC_SECTION,
                        "section": target[0],
                        "offset": patch_image_offset,
                        "addend": target[1],
                    }
                )
                continue

            if r_type == R_AARCH64_PREL64:
                relocs.append(
                    {
                        "type": USER_DLL_RELOC_REL64,
                        "section": target[0],
                        "offset": patch_image_offset,
                        "addend": target[1],
                    }
                )
                continue

            raise ValueError(f"unsupported AArch64 relocation type: {r_type}")

    return relocs


def resolve_entry_symbol(symbol_name: str, by_name: Dict[str, dict], alloc_sections: List[dict]) -> Tuple[int, int]:
    if symbol_name not in by_name:
        raise ValueError(f"required symbol not found in ELF image: {symbol_name}")

    symbol = by_name[symbol_name]
    target = resolve_symbol_reference(symbol, 0, alloc_sections)
    if target is None:
        raise ValueError(f"required symbol is undefined in ELF image: {symbol_name}")
    return target


def is_export_symbol(symbol: dict) -> bool:
    binding = symbol["info"] >> 4
    symbol_type = symbol["info"] & 0xF
    name = symbol["name"]

    if not name or name == USER_DLL_ENTRY_SYMBOL or name.startswith("_") or name.startswith("."):
        return False
    if binding not in (STB_GLOBAL, STB_WEAK):
        return False
    if symbol_type not in (STT_FUNC, STT_OBJECT):
        return False
    if symbol["shndx"] in (SHN_UNDEF, SHN_ABS):
        return False
    return True


def build_exports(symbols: List[dict], alloc_sections: List[dict]) -> List[dict]:
    exports: List[dict] = []
    seen_names: set[str] = set()

    for symbol in symbols:
        if not is_export_symbol(symbol):
            continue
        if symbol["name"] in seen_names:
            continue

        target = resolve_symbol_reference(symbol, 0, alloc_sections)
        if target is None:
            raise ValueError(f"unable to resolve export target for symbol {symbol['name']}")

        exports.append({"name": symbol["name"], "section": target[0], "offset": target[1]})
        seen_names.add(symbol["name"])
        if len(exports) > USER_DLL_MAX_EXPORTS:
            raise ValueError(f"too many DLL exports: {len(exports)} > {USER_DLL_MAX_EXPORTS}")

    return exports


def build_dll_image(elf_data: bytes) -> bytes:
    sections, alloc_sections = build_alloc_sections(elf_data)
    logical_sections = build_flat_sections(alloc_sections)
    relocs = build_relocations(elf_data, sections, alloc_sections, logical_sections)
    _symbols, by_name = load_elf_symbols(elf_data)
    entry_section, entry_offset = resolve_entry_symbol(USER_DLL_ENTRY_SYMBOL, by_name, alloc_sections)
    exports = build_exports(_symbols, alloc_sections)

    metadata_size = USER_DLL_HEADER_FORMAT.size + (len(logical_sections) * USER_DLL_SECTION_FORMAT.size) + (len(relocs) * USER_DLL_RELOC_FORMAT.size) + (len(exports) * USER_DLL_EXPORT_FORMAT.size)
    metadata_size = align_up(metadata_size, USER_DLL_PAYLOAD_ALIGN)

    payload_cursor = metadata_size
    section_entries = bytearray()
    for logical_section in logical_sections:
        section_entries.extend(
            USER_DLL_SECTION_FORMAT.pack(
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

    reloc_entries = bytearray()
    for entry in relocs:
        reloc_entries.extend(
            USER_DLL_RELOC_FORMAT.pack(
                entry["type"],
                entry["section"],
                entry["offset"],
                entry["addend"],
            )
        )

    export_entries = bytearray()
    for entry in exports:
        export_entries.extend(
            USER_DLL_EXPORT_FORMAT.pack(
                encode_fixed_ascii(entry["name"], USER_DLL_EXPORT_NAME_SIZE, "export name"),
                entry["section"],
                entry["offset"],
            )
        )

    image_size = 0
    for logical_section in logical_sections:
        image_size = max(image_size, logical_section["runtime_offset"] + logical_section["mem_size"])
    image_size = align_up(image_size, PAGE_SIZE)

    header = bytearray(
        USER_DLL_HEADER_FORMAT.pack(
            USER_DLL_MAGIC,
            USER_DLL_VERSION,
            ELF_MACHINE_AARCH64,
            0,
            metadata_size,
            len(logical_sections),
            len(relocs),
            len(exports),
            entry_section,
            entry_offset,
            image_size,
            PAGE_SIZE,
        )
    )
    image = bytearray(header)
    image.extend(section_entries)
    image.extend(reloc_entries)
    image.extend(export_entries)
    image.extend(b"\x00" * (metadata_size - len(image)))

    for logical_section in logical_sections:
        image.extend(logical_section["payload"][: logical_section["file_size"]])

    return bytes(image)


def main() -> int:
    args = parse_args()
    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)

    try:
        elf_data, _by_name = validate_elf(input_path, [USER_DLL_ENTRY_SYMBOL])
        dll_image = build_dll_image(elf_data)
    except ValueError as exc:
        print(f"pack_user_dll.py: {exc}", file=sys.stderr)
        return 1

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(dll_image)
    return 0


if __name__ == "__main__":
    sys.exit(main())