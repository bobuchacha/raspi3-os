#!/usr/bin/env python3

import argparse
import json
import pathlib
import struct
import sys


ELF_MAGIC = b"\x7fELF"
ELF_CLASS_64 = 2
ELF_DATA_LSB = 1
ELF_TYPE_EXEC = 2
ELF_MACHINE_AARCH64 = 183
PT_LOAD = 1
PF_X = 0x1
PF_W = 0x2
PF_R = 0x4

USER_EXE_MAGIC = b"ROSXEXE\x00"
USER_EXE_VERSION = 1
USER_EXE_HEADER_SIZE = 512
USER_EXE_SEGMENT_TABLE_OFFSET = 56
USER_EXE_MAX_SEGMENTS = 8
USER_EXE_SEGMENT_SIZE = 48
USER_EXE_FLAG_SYSTEM_EXTENSION = 0x1

SYSTEM_EXTENSION_MAGIC = b"ROSXEXT\x00"
SYSTEM_EXTENSION_VERSION = 1
SYSTEM_EXTENSION_BLOCK_SIZE = 512
SYSTEM_EXTENSION_NAME_SIZE = 64
SYSTEM_EXTENSION_FUNC_NAME_SIZE = 56

ELF_HEADER_FORMAT = struct.Struct("<16sHHIQQQIHHHHHH")
PROGRAM_HEADER_FORMAT = struct.Struct("<IIQQQQQQ")
SECTION_HEADER_FORMAT = struct.Struct("<IIQQQQIIQQ")
SYMBOL_FORMAT = struct.Struct("<IBBHQQ")
USER_EXE_HEADER_FORMAT = struct.Struct("<8sIIQQIIQQ")
USER_EXE_SEGMENT_FORMAT = struct.Struct("<QQQQQII")
SYSTEM_EXTENSION_HEADER_FORMAT = struct.Struct("<8sIIII64sQQ")
SYSTEM_EXTENSION_FUNC_FORMAT = struct.Struct("<56sQ")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Pack a linked user ELF into the ROS user .exe format")
    parser.add_argument("--input", required=True, help="Input ELF path")
    parser.add_argument("--output", required=True, help="Output EXE path")
    parser.add_argument("--extension-descriptor", help="Optional JSON descriptor for a .sys extension")
    return parser.parse_args()


def load_elf_metadata(data: bytes):
    if len(data) < ELF_HEADER_FORMAT.size:
        raise ValueError("ELF file is too small")

    header = ELF_HEADER_FORMAT.unpack_from(data, 0)
    e_ident = header[0]
    e_type = header[1]
    e_machine = header[2]
    e_entry = header[4]
    e_phoff = header[5]
    e_phentsize = header[9]
    e_phnum = header[10]

    if e_ident[:4] != ELF_MAGIC:
        raise ValueError("input is not an ELF file")
    if e_ident[4] != ELF_CLASS_64:
        raise ValueError("expected ELF64 input")
    if e_ident[5] != ELF_DATA_LSB:
        raise ValueError("expected little-endian ELF input")
    if e_type != ELF_TYPE_EXEC:
        raise ValueError("expected ET_EXEC input")
    if e_machine != ELF_MACHINE_AARCH64:
        raise ValueError("expected AArch64 ELF input")
    if e_phentsize != PROGRAM_HEADER_FORMAT.size:
        raise ValueError("unexpected ELF program header size")

    segments = []
    for index in range(e_phnum):
        offset = e_phoff + (index * e_phentsize)
        if offset + PROGRAM_HEADER_FORMAT.size > len(data):
            raise ValueError("ELF program header table is truncated")

        ph = PROGRAM_HEADER_FORMAT.unpack_from(data, offset)
        p_type, p_flags, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz, p_align = ph
        if p_type != PT_LOAD:
            continue
        if p_filesz > p_memsz:
            raise ValueError(f"segment {index} has file size larger than memory size")
        if p_offset + p_filesz > len(data):
            raise ValueError(f"segment {index} points past end of file")

        exe_flags = 0
        if p_flags & PF_R:
            exe_flags |= 0x1
        if p_flags & PF_W:
            exe_flags |= 0x2
        if p_flags & PF_X:
            exe_flags |= 0x4

        segments.append(
            {
                "vaddr": p_vaddr,
                "filesz": p_filesz,
                "memsz": p_memsz,
                "align": p_align,
                "flags": exe_flags,
                "data": data[p_offset : p_offset + p_filesz],
            }
        )

    if not segments:
        raise ValueError("no PT_LOAD segments found in ELF input")
    if len(segments) > USER_EXE_MAX_SEGMENTS:
        raise ValueError(f"too many PT_LOAD segments: {len(segments)} > {USER_EXE_MAX_SEGMENTS}")

    image_base = min(segment["vaddr"] for segment in segments)
    image_end = max(segment["vaddr"] + segment["memsz"] for segment in segments)
    return e_entry, image_base, image_end - image_base, segments


def load_elf_symbols(data: bytes) -> dict[str, int]:
    if len(data) < ELF_HEADER_FORMAT.size:
        raise ValueError("ELF file is too small")

    header = ELF_HEADER_FORMAT.unpack_from(data, 0)
    e_shoff = header[6]
    e_shentsize = header[11]
    e_shnum = header[12]

    if e_shentsize != SECTION_HEADER_FORMAT.size:
        raise ValueError("unexpected ELF section header size")

    sections = []
    for index in range(e_shnum):
        offset = e_shoff + (index * e_shentsize)
        if offset + SECTION_HEADER_FORMAT.size > len(data):
            raise ValueError("ELF section header table is truncated")
        sections.append(SECTION_HEADER_FORMAT.unpack_from(data, offset))

    symbols: dict[str, int] = {}
    for section in sections:
        sh_type = section[1]
        sh_offset = section[4]
        sh_size = section[5]
        sh_link = section[6]
        sh_entsize = section[9]

        if sh_type != 2 or sh_entsize != SYMBOL_FORMAT.size:
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
            st_name, _st_info, _st_other, st_shndx, st_value, _st_size = SYMBOL_FORMAT.unpack_from(data, sym_offset)
            if st_name >= len(strings) or st_shndx == 0:
                continue
            end = strings.find(b"\x00", st_name)
            if end < 0:
                continue
            name = strings[st_name:end].decode("ascii", errors="ignore")
            if name:
                symbols[name] = st_value

    return symbols


def encode_fixed_ascii(text: str, size: int, field_name: str) -> bytes:
    encoded = text.encode("ascii")
    if len(encoded) >= size:
        raise ValueError(f"{field_name} is too long: {text}")
    return encoded + (b"\x00" * (size - len(encoded)))


def build_extension_descriptor(descriptor_path: pathlib.Path, elf_data: bytes) -> bytes:
    descriptor = json.loads(descriptor_path.read_text())
    symbols = load_elf_symbols(elf_data)

    name = descriptor.get("name")
    functions = descriptor.get("functions")
    init_name = descriptor.get("init", "")
    loop_name = descriptor.get("loop", "")
    if not isinstance(name, str) or not name:
        raise ValueError("extension descriptor requires a non-empty 'name'")
    if not isinstance(functions, list) or not functions:
        raise ValueError("extension descriptor requires a non-empty 'functions' list")

    init_address = 0
    if init_name:
        if init_name not in symbols:
            raise ValueError(f"extension init symbol not found: {init_name}")
        init_address = symbols[init_name]

    loop_address = 0
    if loop_name:
        if loop_name not in symbols:
            raise ValueError(f"extension loop symbol not found: {loop_name}")
        loop_address = symbols[loop_name]

    records = bytearray()
    for func_name in functions:
        if not isinstance(func_name, str) or not func_name:
            raise ValueError("extension function names must be non-empty strings")
        if func_name not in symbols:
            raise ValueError(f"extension function symbol not found: {func_name}")
        records.extend(
            SYSTEM_EXTENSION_FUNC_FORMAT.pack(
                encode_fixed_ascii(func_name, SYSTEM_EXTENSION_FUNC_NAME_SIZE, "function name"),
                symbols[func_name],
            )
        )

    extra_blocks = (len(records) + SYSTEM_EXTENSION_BLOCK_SIZE - 1) // SYSTEM_EXTENSION_BLOCK_SIZE
    block_count = 1 + extra_blocks
    header_block = bytearray(
        SYSTEM_EXTENSION_HEADER_FORMAT.pack(
            SYSTEM_EXTENSION_MAGIC,
            SYSTEM_EXTENSION_VERSION,
            block_count,
            len(functions),
            0,
            encode_fixed_ascii(name, SYSTEM_EXTENSION_NAME_SIZE, "extension name"),
            init_address,
            loop_address,
        )
    )
    header_block.extend(b"\x00" * (SYSTEM_EXTENSION_BLOCK_SIZE - len(header_block)))
    records.extend(b"\x00" * ((extra_blocks * SYSTEM_EXTENSION_BLOCK_SIZE) - len(records)))
    return bytes(header_block) + bytes(records)


def build_executable(entry_point: int, image_base: int, image_size: int, segments, descriptor_bytes: bytes = b""):
    payload = bytearray()
    segment_table = bytearray()

    for segment in segments:
        file_offset = USER_EXE_HEADER_SIZE + len(descriptor_bytes) + len(payload)
        payload.extend(segment["data"])
        segment_table.extend(
            USER_EXE_SEGMENT_FORMAT.pack(
                file_offset,
                segment["vaddr"],
                segment["filesz"],
                segment["memsz"],
                segment["align"],
                segment["flags"],
                0,
            )
        )

    segment_table.extend(b"\x00" * ((USER_EXE_MAX_SEGMENTS - len(segments)) * USER_EXE_SEGMENT_SIZE))

    header = bytearray(
        USER_EXE_HEADER_FORMAT.pack(
            USER_EXE_MAGIC,
            USER_EXE_VERSION,
            USER_EXE_HEADER_SIZE,
            entry_point,
            USER_EXE_SEGMENT_TABLE_OFFSET,
            len(segments),
            USER_EXE_FLAG_SYSTEM_EXTENSION if descriptor_bytes else 0,
            image_base,
            image_size,
        )
    )
    header.extend(segment_table)
    if len(header) > USER_EXE_HEADER_SIZE:
        raise ValueError("header layout exceeded 512 bytes")
    header.extend(b"\x00" * (USER_EXE_HEADER_SIZE - len(header)))

    return bytes(header) + descriptor_bytes + bytes(payload)


def main() -> int:
    args = parse_args()
    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)
    data = input_path.read_bytes()

    try:
        entry_point, image_base, image_size, segments = load_elf_metadata(data)
        descriptor_bytes = b""
        if args.extension_descriptor:
            descriptor_bytes = build_extension_descriptor(pathlib.Path(args.extension_descriptor), data)
        executable = build_executable(entry_point, image_base, image_size, segments, descriptor_bytes)
    except ValueError as exc:
        print(f"pack_user_exe.py: {exc}", file=sys.stderr)
        return 1

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(executable)
    return 0


if __name__ == "__main__":
    sys.exit(main())