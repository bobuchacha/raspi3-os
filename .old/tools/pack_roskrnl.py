#!/usr/bin/env python3

import argparse
import pathlib
import struct
import sys

ELF_MAGIC = b"\x7fELF"
ELF_CLASS_64 = 2
ELF_DATA_LSB = 1
ELF_TYPE_EXEC = 2
ELF_MACHINE_AARCH64 = 183
PT_LOAD = 1

ROSKRNL_MAGIC = b"ROSKRNL\x00"
ROSKRNL_VERSION = 1
ROSKRNL_HEADER_FORMAT = struct.Struct("<8sIIIIQQQQQQQ")

ELF_HEADER_FORMAT = struct.Struct("<16sHHIQQQIHHHHHH")
PROGRAM_HEADER_FORMAT = struct.Struct("<IIQQQQQQ")

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Pack the linked kernel ELF into the ROSKRNL loader format")
    parser.add_argument("--input", required=True, help="Input kernel ELF path")
    parser.add_argument("--output", required=True, help="Output ROSKRNL path")
    return parser.parse_args()

def load_kernel_segment(data: bytes):
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

    load_segments = []
    for index in range(e_phnum):
        offset = e_phoff + (index * e_phentsize)
        if offset + PROGRAM_HEADER_FORMAT.size > len(data):
            raise ValueError("ELF program header table is truncated")

        ph = PROGRAM_HEADER_FORMAT.unpack_from(data, offset)
        p_type, _p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = ph
        if p_type != PT_LOAD:
            continue
        if p_filesz > p_memsz:
            raise ValueError(f"segment {index} has file size larger than memory size")
        if p_offset + p_filesz > len(data):
            raise ValueError(f"segment {index} points past end of file")
        load_segments.append(
            {
                "vaddr": p_vaddr,
                "paddr": p_paddr,
                "filesz": p_filesz,
                "memsz": p_memsz,
                "align": p_align,
                "payload": data[p_offset : p_offset + p_filesz],
            }
        )

    if not load_segments:
        raise ValueError("expected at least one PT_LOAD segment")

    load_segments.sort(key=lambda segment: segment["paddr"])
    base_segment = load_segments[0]
    base_delta = base_segment["paddr"] - base_segment["vaddr"]
    merged_filesz = 0
    merged_memsz = 0
    merged_align = 0

    for index, segment in enumerate(load_segments):
        if segment["paddr"] - segment["vaddr"] != base_delta:
            raise ValueError(f"PT_LOAD segment {index} uses a different phys/virt bias")

        file_end = (segment["paddr"] - base_segment["paddr"]) + segment["filesz"]
        mem_end = (segment["paddr"] - base_segment["paddr"]) + segment["memsz"]
        if file_end > merged_filesz:
            merged_filesz = file_end
        if mem_end > merged_memsz:
            merged_memsz = mem_end
        if segment["align"] > merged_align:
            merged_align = segment["align"]

    merged_payload = bytearray(merged_filesz)
    for segment in load_segments:
        payload_offset = segment["paddr"] - base_segment["paddr"]
        merged_payload[payload_offset : payload_offset + segment["filesz"]] = segment["payload"]

    return {
        "entry": e_entry,
        "vaddr": base_segment["vaddr"],
        "paddr": base_segment["paddr"],
        "filesz": merged_filesz,
        "memsz": merged_memsz,
        "align": merged_align,
        "payload": bytes(merged_payload),
    }

def build_roskrnl_image(kernel_segment: dict) -> bytes:
    header_size = ROSKRNL_HEADER_FORMAT.size
    header = ROSKRNL_HEADER_FORMAT.pack(
        ROSKRNL_MAGIC,
        ROSKRNL_VERSION,
        header_size,
        ELF_MACHINE_AARCH64,
        0,
        kernel_segment["paddr"],
        kernel_segment["vaddr"],
        kernel_segment["entry"],
        header_size,
        kernel_segment["filesz"],
        kernel_segment["memsz"],
        kernel_segment["align"],
    )
    return header + kernel_segment["payload"]

def main() -> int:
    args = parse_args()
    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)

    try:
        kernel_segment = load_kernel_segment(input_path.read_bytes())
        image = build_roskrnl_image(kernel_segment)
    except ValueError as exc:
        print(f"pack_roskrnl.py: {exc}", file=sys.stderr)
        return 1

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(image)
    return 0

if __name__ == "__main__":
    sys.exit(main())