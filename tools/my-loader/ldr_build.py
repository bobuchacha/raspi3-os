#!/usr/bin/env python3
"""
ldr_build.py

Build helper that compiles AArch64 sources and packs loader artifacts into a
Windows-like custom `.exe`, `.dll`, and `.sys` image format.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Dict, Any, Tuple

MAGIC = 0x304C4C44  # "DLL0"
VERSION_MAJOR = 1
VERSION_MINOR = 0
DLL_MACHINE_AARCH64 = 0xAA64
DLL_SECTION_ALIGNMENT = 0x1000
DLL_FILE_ALIGNMENT = 0x200
DLL_DEFAULT_EXE_BASE = 0x00200000
DLL_DEFAULT_DLL_BASE = 0x00600000
DLL_DEFAULT_SYS_BASE = 0x01600000

ELF_MAGIC = b"\x7fELF"
ELF_CLASS_64 = 2
ELF_DATA_LSB = 1
ELF_TYPE_REL = 1
ELF_TYPE_EXEC = 2
ELF_MACHINE_AARCH64 = 183
SHT_SYMTAB = 2
SHT_RELA = 4
SHT_DYNSYM = 11
SHN_UNDEF = 0
SHN_ABS = 0xFFF1
STB_GLOBAL = 1
STB_WEAK = 2
STT_OBJECT = 1
STT_FUNC = 2
R_AARCH64_ABS64 = 257
R_AARCH64_PREL64 = 260
R_AARCH64_GLOB_DAT = 1025
R_AARCH64_JUMP_SLOT = 1026
R_AARCH64_RELATIVE = 1027
PT_LOAD = 1

KERNEL_IMAGE_MAGIC = b"KERNEL\x00\x00"
KERNEL_IMAGE_VERSION = 1
KERNEL_IMAGE_HEADER_FORMAT = struct.Struct("<8sIIIIQQQQQQQ")

ELF_HEADER_FORMAT = struct.Struct("<16sHHIQQQIHHHHHH")
PROGRAM_HEADER_FORMAT = struct.Struct("<IIQQQQQQ")
SECTION_HEADER_FORMAT = struct.Struct("<IIQQQQIIQQ")
SYMBOL_FORMAT = struct.Struct("<IBBHQQ")
RELA_FORMAT = struct.Struct("<QQq")

LDR_META_VERSION = 2
LDR_META_KIND_IMPORT = 1
LDR_META_KIND_EXPORT = 2
LDR_META_NAME_MAX = 64
LDR_META_IMPORT_SECTION = ".ldrmeta.imports"
LDR_META_EXPORT_SECTION = ".ldrmeta.exports"
LDR_IMPORT_META_FORMAT = struct.Struct(f"<HH{LDR_META_NAME_MAX}s{LDR_META_NAME_MAX}s{LDR_META_NAME_MAX}s")
LDR_EXPORT_META_V1_FORMAT = struct.Struct(f"<HH{LDR_META_NAME_MAX}s")
LDR_EXPORT_META_FORMAT = struct.Struct(f"<HH{LDR_META_NAME_MAX}s{LDR_META_NAME_MAX}s")

KERNEL_MODULE_METADATA_SECTION = ".ros.module.meta"
KERNEL_MODULE_METADATA_MAGIC = b"ROSKMETA"
KERNEL_MODULE_METADATA_VERSION = 1
KERNEL_MODULE_EXPORT_MAX = 4
KERNEL_MODULE_METADATA_HEADER_FORMAT = struct.Struct("<8sII64s64s64s64sI")
KERNEL_MODULE_METADATA_EXPORT_FORMAT = struct.Struct("<16s64s")

KERNEL_MODULE_INIT_EXPORT = "__ros_init"
KERNEL_MODULE_SHUTDOWN_EXPORT = "__ros_shutdown"
KERNEL_MODULE_IDLE_EXPORT = "__ros_idle"

KIND_MAP = {
    "exe": 1,
    "dll": 2,
    "sys": 3,
}

DLL_HEADER_FORMAT = struct.Struct("<IHHHHIQQQQQQIIIIIIIIIIIIII")
DLL_SECTION_FORMAT = struct.Struct("<8sQQQQII")
DLL_IMPORT_MODULE_FORMAT = struct.Struct("<IIII")
DLL_IMPORT_SYMBOL_FORMAT = struct.Struct("<IIII")
DLL_EXPORT_FORMAT = struct.Struct("<IIQ")
DLL_RELOCATION_FORMAT = struct.Struct("<IIQ")
DLL_RELOC_ABS64 = 1
DLL_RELOC_ABS32 = 2


@dataclass
class SectionInfo:
    """Represents one section extracted from ELF metadata."""

    name: str
    flags: int
    rva: int
    file_off: int
    file_size: int
    virt_size: int
    align: int
    section_index: int


@dataclass
class SymbolInfo:
    """Represents one symbol extracted from ELF metadata."""

    name: str
    value: int
    section_index: int


@dataclass
class RelocationInfo:
    """Represents one relocation extracted from ELF metadata."""

    patch_rva: int
    type: int
    addend: int


@dataclass
class RawSymbolInfo:
    """Represents one ELF symbol with binding/type metadata."""

    name: str
    info: int
    section_index: int
    value: int


@dataclass
class BuildConfig:
    """Normalized build config parsed from JSON recipe."""

    name: str
    kind: str
    sources: List[str]
    include_dirs: List[str]
    defines: List[str]
    cflags: List[str]
    ldflags: List[str]
    entry_symbol: str
    imports: List[Dict[str, str]]
    exports: List[Dict[str, str]]
    output_dir: str


def build_config_from_raw(raw: Dict[str, Any], source_label: str) -> BuildConfig:
    """
    Normalize one raw build configuration from either JSON or CLI arguments.

    Args:
        raw: Raw config dictionary.
        source_label: Human-readable source label for diagnostics.

    Returns:
        Parsed BuildConfig object.
    """
    kind = str(raw.get("kind", "")).lower()
    if kind not in KIND_MAP:
        raise ValueError(f"{source_label}: kind must be one of: exe, dll, sys")

    legacy_imports = raw.get("imports")
    legacy_exports = raw.get("exports")
    if legacy_imports not in (None, [], {}):
        raise ValueError(
            f"{source_label}: JSON imports are no longer supported; declare imports inline with LDR_IMPORT in source files"
        )
    if legacy_exports not in (None, [], {}):
        raise ValueError(
            f"{source_label}: JSON exports are no longer supported; declare exports inline with LDR_EXPORT in source files"
        )

    name = str(raw.get("name", "")).strip()
    if not name:
        raise ValueError(f"{source_label}: name is required")

    sources = [str(src).strip() for src in raw.get("sources", []) if str(src).strip()]
    if not sources:
        raise ValueError(f"{source_label}: at least one source is required")

    return BuildConfig(
        name=name,
        kind=kind,
        sources=sources,
        include_dirs=[str(value).strip() for value in raw.get("include_dirs", []) if str(value).strip()],
        defines=[str(value).strip() for value in raw.get("defines", []) if str(value).strip()],
        cflags=[str(value).strip() for value in raw.get("cflags", []) if str(value).strip()],
        ldflags=[str(value).strip() for value in raw.get("ldflags", []) if str(value).strip()],
        entry_symbol=str(raw.get("entry_symbol", "Init")).strip() or "Init",
        imports=[],
        exports=[],
        output_dir=str(raw.get("output_dir", "build")).strip() or "build",
    )


def normalize_import_spec(index: int, spec: Dict[str, Any], source: str = "import") -> Tuple[str, str, str]:
    """
    Validate and normalize one import specification.

    Args:
        index: Index of the import within its source list.
        spec: Raw metadata dictionary.
        source: Human-readable source label used in error messages.

    Returns:
        Tuple(module, symbol, slot_symbol).
    """
    module = str(spec.get("module", "")).strip()
    symbol = str(spec.get("symbol", "")).strip()
    slot_symbol = str(spec.get("slot_symbol", "")).strip()
    if not module or not symbol or not slot_symbol:
        raise RuntimeError(f"invalid {source}[{index}] - expected module/symbol/slot_symbol")
    return module, symbol, slot_symbol


def normalize_export_spec(index: int, spec: Dict[str, Any], source: str = "export") -> Tuple[str, str]:
    """
    Validate and normalize one export specification.

    Args:
        index: Index of the export within its source list.
        spec: Raw metadata dictionary.
        source: Human-readable source label used in error messages.

    Returns:
        Tuple(export_name, symbol_name).
    """
    symbol = str(spec.get("symbol", "")).strip()
    if not symbol:
        raise RuntimeError(f"invalid {source}[{index}] - expected symbol")
    export_name = str(spec.get("name", symbol)).strip() or symbol
    return export_name, symbol


def read_meta_string(raw: bytes, field_name: str, source: str) -> str:
    """
    Decode one fixed-size metadata string field.

    Args:
        raw: Fixed-width NUL-padded byte array.
        field_name: Field label used in diagnostics.
        source: Human-readable source label used in diagnostics.

    Returns:
        Decoded UTF-8 string without trailing NUL padding.
    """
    end = raw.find(b"\x00")
    if end < 0:
        raise RuntimeError(f"{source} field '{field_name}' is not NUL-terminated")
    return raw[:end].decode("utf-8")


def decode_fixed_ascii(raw: bytes) -> str:
    """
    Decode one fixed-width NUL-padded ASCII string.

    Args:
        raw: Fixed-width byte array.

    Returns:
        Decoded string with trailing NUL padding removed.
    """
    return raw.split(b"\x00", 1)[0].decode("ascii", errors="ignore")


def run(cmd: List[str], cwd: Path | None = None) -> None:
    """
    Execute one subprocess and fail fast on non-zero status.

    Args:
        cmd: Full command argv list.
        cwd: Optional working directory.

    Returns:
        None. Raises RuntimeError if command fails.
    """
    print("[cmd]", " ".join(cmd))
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None, capture_output=True, text=True)
    if proc.returncode != 0:
        print(proc.stdout)
        print(proc.stderr, file=sys.stderr)
        raise RuntimeError(f"command failed with exit={proc.returncode}: {' '.join(cmd)}")


def write_bytes_atomic(path: Path, data: bytes) -> None:
    """
    Atomically replace one output file.

    This keeps interrupted pack/link runs from leaving truncated artifacts that
    later look up to date to `make` but are no longer loadable.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_name(f".{path.name}.tmp")
    try:
        temp_path.write_bytes(data)
        temp_path.replace(path)
    finally:
        if temp_path.exists():
            temp_path.unlink()


def build_config_from_args(args: argparse.Namespace) -> BuildConfig:
    """
    Build config from direct CLI flags.

    Args:
        args: Parsed argparse namespace.

    Returns:
        Parsed BuildConfig object.
    """
    raw: Dict[str, Any] = {
        "name": args.name,
        "kind": args.kind,
        "sources": args.source or [],
        "include_dirs": args.include_dir or [],
        "defines": args.define or [],
        "cflags": args.cflag or [],
        "ldflags": args.ldflag or [],
        "entry_symbol": args.entry_symbol,
        "output_dir": args.output_dir,
    }
    return build_config_from_raw(raw, "command line")


def tool(name: str) -> str:
    """
    Resolve AArch64 cross tool executable path by expected prefix.

    Args:
        name: Tool suffix, e.g. 'gcc', 'ld', 'readelf'.

    Returns:
        Tool name string used in subprocess execution.
    """
    return f"aarch64-none-elf-{name}"


def find_armgnu_prefixed(name: str) -> str | None:
    """
    Resolve one Arm GNU prefixed tool, including common absolute install paths.

    Args:
        name: Tool suffix, e.g. 'gcc', 'ld', 'readelf'.

    Returns:
        Executable path or name if found, otherwise None.
    """
    candidates: List[str] = []
    armgnu = os.environ.get("ARMGNU", "").strip()

    if armgnu:
        candidates.append(f"{armgnu}-{name}")

    for bin_dir in sorted(glob.glob("/Applications/ArmGNUToolchain/*/aarch64-none-elf/bin"), reverse=True):
        prefix = str(Path(bin_dir) / "aarch64-none-elf")
        candidates.append(f"{prefix}-{name}")

    candidates.append(tool(name))

    for candidate in candidates:
        if "/" in candidate:
            if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
                return candidate
            continue
        found = shutil.which(candidate)
        if found:
            return found

    return None


def first_tool(*names: str) -> str | None:
    """
    Return the first executable name available in PATH.

    Args:
        names: Candidate executable names in lookup order.

    Returns:
        Executable name string if found, otherwise None.
    """
    for name in names:
        if shutil.which(name):
            return name
    return None


def resolve_compiler() -> List[str]:
    """
    Resolve compiler command for AArch64 freestanding objects.

    Args:
        None.

    Returns:
        Command prefix list for subprocess execution.
    """
    gcc = find_armgnu_prefixed("gcc")
    if gcc:
        return [gcc]

    clang = first_tool("clang")
    if clang:
        # Force ELF/AArch64 output from Apple clang when GNU toolchain is absent.
        return [clang, "--target=aarch64-none-elf"]

    raise RuntimeError("no AArch64 compiler found: expected aarch64-none-elf-gcc or clang")


def resolve_cxx_compiler() -> List[str]:
    """
    Resolve compiler command for AArch64 freestanding C++ objects.

    Args:
        None.

    Returns:
        Command prefix list for subprocess execution.
    """
    gxx = find_armgnu_prefixed("g++")
    if gxx:
        return [gxx]

    gcc = find_armgnu_prefixed("gcc")
    if gcc:
        return [gcc]

    clangxx = first_tool("clang++")
    if clangxx:
        return [clangxx, "--target=aarch64-none-elf"]

    clang = first_tool("clang")
    if clang:
        return [clang, "--target=aarch64-none-elf"]

    raise RuntimeError("no AArch64 C++ compiler found: expected aarch64-none-elf-g++ or clang++")


def is_cxx_source(path: Path) -> bool:
    """
    Check whether one source path should be compiled as C++.

    Args:
        path: Source file path.

    Returns:
        True when the file suffix is a known C++ extension.
    """
    return path.suffix.lower() in (".cpp", ".cc", ".cxx")


def is_runtime_support_source(source_path: Path, project_root: Path) -> bool:
    """
    Check whether one source belongs to the injected userspace runtime layer.

    Runtime support objects are linked into apps/DLLs/SYS modules for libc-like
    helpers, but they are not part of the module's public ABI and must not be
    auto-exported.
    """
    try:
        relative_path = source_path.resolve().relative_to(project_root.resolve())
    except ValueError:
        return False

    return relative_path.parts[:2] == ("applications", "runtime")


def object_variant_key(cfg: BuildConfig, project_root: Path, source_path: Path) -> str:
    """
    Return one stable cache key for the compile settings of a source file.

    Userspace runtime sources such as `cpp_runtime.cpp` are injected into many
    artifacts, but they are not all compiled with the same flags. EXE, DLL, and
    GUI-specific builds vary by include roots and `-fPIC`, so a cache key that
    only uses the source path causes targets to overwrite each other's objects
    and forces rebuild churn on every subsequent artifact.

    Args:
        cfg: Build recipe for the current artifact.
        project_root: Repository root used to resolve include directories.
        source_path: Source path being compiled.

    Returns:
        Short hexadecimal cache key suitable for embedding in object filenames.
    """
    compiler = resolve_cxx_compiler() if is_cxx_source(source_path) else resolve_compiler()
    signature_parts: List[str] = list(compiler)

    signature_parts.extend(["-c", "-ffreestanding", "-fno-builtin", "-fno-stack-protector"])
    signature_parts.extend(cfg.cflags)
    for inc in cfg.include_dirs:
        signature_parts.extend(["-I", str((project_root / inc).resolve())])
    for define in cfg.defines:
        signature_parts.append(f"-D{define}")
    if cfg.kind == "dll":
        signature_parts.append("-fPIC")
    if is_cxx_source(source_path):
        signature_parts.extend(["-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-fno-use-cxa-atexit"])

    return hashlib.sha1("\n".join(signature_parts).encode("utf-8")).hexdigest()[:12]


def object_path_for_source(cfg: BuildConfig, project_root: Path, build_dir: Path, source_path: Path) -> Path:
    """
    Derive one stable unique object path for a source file.

    The builder injects support sources like `applications/runtime/ros_support.c`
    into many targets. Using only `stem + .o` makes source-to-object mapping
    ambiguous and risks collisions when different directories reuse `main.c`.
    The compile-variant suffix also keeps EXE and DLL builds from fighting over
    the same cached object when their flags differ.
    """
    try:
        relative_path = source_path.resolve().relative_to(project_root.resolve())
        stem = "__".join(relative_path.with_suffix("").parts)
    except ValueError:
        stem = source_path.stem

    variant = object_variant_key(cfg, project_root, source_path)
    return build_dir / f"{stem}__{variant}.o"


def depfile_path_for_object(obj_path: Path) -> Path:
    """
    Return the dependency-file path associated with one compiled object.

    GCC and clang can emit Make-style dependency files during compilation.
    Keeping the depfile beside the object lets the builder detect header-only
    changes without recompiling every source in the same image on every run.

    Args:
        obj_path: Object-file path produced from one source.

    Returns:
        Sidecar dependency-file path.
    """
    return obj_path.with_suffix(".d")


def command_path_for_object(obj_path: Path) -> Path:
    """
    Return the command-signature path associated with one compiled object.

    Incremental object reuse must invalidate cleanly when compiler flags,
    include roots, defines, or the compiler binary itself change. This sidecar
    stores the exact compile argv so the builder can detect configuration drift.

    Args:
        obj_path: Object-file path produced from one source.

    Returns:
        Sidecar command-signature path.
    """
    return obj_path.with_suffix(".cmd")


def artifact_depfile_path(artifact_path: Path) -> Path:
    """
    Return the Make dependency manifest path associated with one artifact.

    Make needs one dependency file whose target matches the final `.exe` or
    `.dll` path, otherwise header-only changes never wake the artifact target at
    the top-level build graph. Keeping the manifest beside the final artifact
    makes it easy for the Makefile to include every userspace depfile with a
    single `$(addsuffix .d, ...)` expansion.

    Args:
        artifact_path: Final packaged artifact path.

    Returns:
        Sidecar dependency-manifest path.
    """
    return artifact_path.with_name(f"{artifact_path.name}.d")


def make_path_literal(path: Path, project_root: Path) -> str:
    """
    Convert one filesystem path into the spelling that Make should consume.

    Top-level userspace targets are declared with project-relative output paths
    like `output/boards/virt/userspace/core.exe`. If the generated depfile used
    absolute target paths instead, GNU Make would treat them as a different
    target and ignore header-only changes for the declared artifact rule.

    Args:
        path: Filesystem path to serialize.
        project_root: Repository root used for relative conversion.

    Returns:
        Make-safe path text.
    """
    candidate_path = path if path.is_absolute() else (project_root / path)

    try:
        literal = candidate_path.resolve().relative_to(project_root.resolve()).as_posix()
    except ValueError:
        literal = str(candidate_path.resolve())

    return literal.replace(" ", "\\ ")


def write_artifact_depfile(cfg: BuildConfig, artifact_path: Path, project_root: Path, build_dir: Path) -> None:
    """
    Write one final-artifact dependency manifest for GNU Make.

    The object-level depfiles produced during compilation are enough for the
    Python builder to skip unchanged sources, but Make still needs one rule that
    says `artifact -> headers` so header-only edits will rerun the builder in
    the first place. This helper folds all per-object prerequisites into a
    single artifact-scoped depfile.

    Args:
        cfg: Build recipe for the current artifact.
        artifact_path: Final packaged artifact path.
        project_root: Repository root used for relative path emission.
        build_dir: Directory where object/dep sidecars live.

    Returns:
        None.
    """
    prerequisite_literals: List[str] = []
    seen_literals: set[str] = set()

    for src in cfg.sources:
        source_path = (project_root / src).resolve()
        object_path = object_path_for_source(cfg, project_root, build_dir, source_path)
        depfile_path = depfile_path_for_object(object_path)
        source_literal = make_path_literal(source_path, project_root)

        if source_literal not in seen_literals:
            prerequisite_literals.append(source_literal)
            seen_literals.add(source_literal)

        if not depfile_path.exists():
            continue

        for prerequisite in parse_make_depfile(depfile_path):
            prerequisite_literal = make_path_literal(prerequisite, project_root)
            if prerequisite_literal in seen_literals:
                continue
            prerequisite_literals.append(prerequisite_literal)
            seen_literals.add(prerequisite_literal)

    # Use chr(92) to emit a single backslash character without escaping hell
    backslash = chr(92)
    depfile_lines = [f"{make_path_literal(artifact_path, project_root)}: " + backslash]
    for index, prerequisite_literal in enumerate(prerequisite_literals):
        suffix = (" " + backslash) if index + 1 < len(prerequisite_literals) else ""
        depfile_lines.append(f"  {prerequisite_literal}{suffix}")

    artifact_depfile_path(artifact_path).write_text("\n".join(depfile_lines) + "\n", encoding="utf-8")


def serialize_command_signature(cmd: List[str]) -> str:
    """
    Serialize one compile command into a stable comparison string.

    Args:
        cmd: Full compiler argv list.

    Returns:
        Stable text representation of the compile command.
    """
    return "\n".join(cmd) + "\n"


def parse_make_depfile(depfile_path: Path) -> List[Path]:
    """
    Parse one Make-style dependency file into a flat prerequisite list.

    The dependency files emitted by `-MMD` use line continuations and a leading
    `target:` field. The builder only needs the prerequisite paths so it can
    compare mtimes against the compiled object.

    Args:
        depfile_path: Path to the dependency file produced for one object.

    Returns:
        List of prerequisite paths referenced by the depfile.
    """
    raw_text = depfile_path.read_text(encoding="utf-8")
    normalized = raw_text.replace("\\\n", " ")
    prerequisites: List[Path] = []

    for logical_line in normalized.splitlines():
        if not logical_line.strip():
            continue

        target_and_deps = logical_line.split(":", 1)
        if len(target_and_deps) != 2:
            continue

        for token in target_and_deps[1].split():
            prerequisites.append(Path(token))

    return prerequisites


def object_is_up_to_date(obj_path: Path, depfile_path: Path, command_path: Path, command_signature: str) -> bool:
    """
    Check whether one compiled object can be safely reused.

    Reuse is allowed only when the object, depfile, and recorded compile command
    all exist, the command signature still matches, and every source/header in
    the depfile is older than the object.

    Args:
        obj_path: Candidate object file.
        depfile_path: Sidecar dependency file.
        command_path: Sidecar command-signature file.
        command_signature: Current compile-command signature.

    Returns:
        True when the object can be reused as-is.
    """
    if not obj_path.exists() or not depfile_path.exists() or not command_path.exists():
        return False

    if command_path.read_text(encoding="utf-8") != command_signature:
        return False

    object_mtime = obj_path.stat().st_mtime
    prerequisites = parse_make_depfile(depfile_path)
    if not prerequisites:
        return False

    for prerequisite in prerequisites:
        if not prerequisite.exists():
            return False
        if prerequisite.stat().st_mtime > object_mtime:
            return False

    return True


def module_object_paths(cfg: BuildConfig, project_root: Path, build_dir: Path) -> List[Path]:
    """
    Return object files that belong to the module itself, excluding injected
    runtime support sources.
    """
    objects: List[Path] = []

    for src in cfg.sources:
        source_path = (project_root / src).resolve()
        if is_runtime_support_source(source_path, project_root):
            continue
        objects.append(object_path_for_source(cfg, project_root, build_dir, source_path))

    return objects


def resolve_linker() -> List[str] | None:
    """
    Resolve standalone linker command for AArch64 ELF images.

    Args:
        None.

    Returns:
        Command prefix list when linker exists, otherwise None.
    """
    linker = find_armgnu_prefixed("ld")
    if linker:
        return [linker]
    return None


def compile_sources(cfg: BuildConfig, project_root: Path, build_dir: Path) -> List[Path]:
    """
    Compile all C sources to object files.

    Args:
        cfg: Build recipe.
        project_root: Project root used for source resolution.
        build_dir: Directory where object files are written.

    Returns:
        List of compiled object file paths.
    """
    objects: List[Path] = []

    # Build common compiler arguments once and reuse for every source.
    common_flags: List[str] = ["-c", "-ffreestanding", "-fno-builtin", "-fno-stack-protector"]
    common_flags.extend(cfg.cflags)
    for inc in cfg.include_dirs:
        common_flags.extend(["-I", str((project_root / inc).resolve())])
    for define in cfg.defines:
        common_flags.append(f"-D{define}")

    for src in cfg.sources:
        src_path = (project_root / src).resolve()
        obj_path = object_path_for_source(cfg, project_root, build_dir, src_path)
        depfile_path = depfile_path_for_object(obj_path)
        command_path = command_path_for_object(obj_path)
        compiler = resolve_cxx_compiler() if is_cxx_source(src_path) else resolve_compiler()
        source_flags = list(common_flags)

        if cfg.kind == "dll":
            source_flags.append("-fPIC")

        if is_cxx_source(src_path):
            source_flags.extend(["-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-fno-use-cxa-atexit"])

        obj_path.parent.mkdir(parents=True, exist_ok=True)

        # Compile each source independently so diagnostics map to a single file.
        cmd = compiler + source_flags + [
            "-MMD",
            "-MF",
            str(depfile_path),
            "-MT",
            str(obj_path),
            str(src_path),
            "-o",
            str(obj_path),
        ]
        command_signature = serialize_command_signature(cmd)

        if object_is_up_to_date(obj_path, depfile_path, command_path, command_signature):
            print(f"[skip] {src_path}")
        else:
            run(cmd)
            command_path.write_text(command_signature, encoding="utf-8")

        objects.append(obj_path)

    return objects


def link_elf(cfg: BuildConfig, objects: List[Path], build_dir: Path) -> Path:
    """
    Link object files into one ELF image.

    Args:
        cfg: Build recipe.
        objects: Object list from compile stage.
        build_dir: Build output directory.

    Returns:
        Path to linked ELF image.
    """
    elf_path = build_dir / f"{cfg.name}.elf"
    temp_elf_path = build_dir / f".{cfg.name}.elf.tmp"

    linker = resolve_linker()
    if linker:
        # Link with explicit entry symbol so runtime starts at expected function.
        cmd = linker + ["-nostdlib", "-e", cfg.entry_symbol]
        if cfg.kind == "dll":
            # Prefer the GNU linker when it is present so DLL builds work even on
            # toolchains that do not ship an lld-compatible compiler driver.
            cmd.append("-shared")
        for export_spec in cfg.exports:
            export_symbol = str(export_spec.get("symbol", "")).strip()
            if export_symbol:
                cmd.extend(["--undefined", export_symbol])
        cmd.extend(cfg.ldflags)
        cmd.extend(str(o) for o in objects)
        cmd.extend(["-o", str(temp_elf_path)])
        run(cmd)
        temp_elf_path.replace(elf_path)
        return elf_path

    # Fallback: use compiler driver + lld when GNU ld is unavailable.
    driver = resolve_cxx_compiler() if any(Path(source).suffix.lower() in (".cpp", ".cc", ".cxx") for source in cfg.sources) else resolve_compiler()
    cmd = driver + ["-nostdlib", "-fuse-ld=lld"]
    if cfg.kind == "dll":
        cmd.append("-shared")
        cmd.append(f"-Wl,-e,{cfg.entry_symbol}")
    else:
        cmd.append(f"-Wl,-e,{cfg.entry_symbol}")
    for export_spec in cfg.exports:
        export_symbol = str(export_spec.get("symbol", "")).strip()
        if export_symbol:
            cmd.append(f"-Wl,--undefined,{export_symbol}")
    for flag in cfg.ldflags:
        cmd.append(f"-Wl,{flag}")
    cmd.extend(str(o) for o in objects)
    cmd.extend(["-o", str(temp_elf_path)])
    run(cmd)
    temp_elf_path.replace(elf_path)
    return elf_path


def align_up(value: int, align: int) -> int:
    """
    Align integer upward to the specified power-of-two boundary.

    Args:
        value: Current offset or address.
        align: Alignment requirement.

    Returns:
        Aligned value.
    """
    if align <= 1:
        return value
    return (value + align - 1) & ~(align - 1)


def align_down(value: int, align: int) -> int:
    """
    Align integer downward to the specified power-of-two boundary.

    Args:
        value: Current offset or address.
        align: Alignment requirement.

    Returns:
        Aligned value.
    """
    if align <= 1:
        return value
    return value & ~(align - 1)


def read_c_string(blob: bytes, off: int) -> str:
    """
    Read one NUL-terminated string from a raw string table.

    Args:
        blob: String table bytes.
        off: Start offset inside the blob.

    Returns:
        Decoded string, or empty string for invalid offsets.
    """
    if off < 0 or off >= len(blob):
        return ""
    end = blob.find(b"\x00", off)
    if end < 0:
        end = len(blob)
    return blob[off:end].decode("utf-8", errors="ignore")


def load_elf_symbols(elf_path: Path) -> List[RawSymbolInfo]:
    """
    Load ELF symbols from SYMTAB/DYNSYM for export synthesis.

    Args:
        elf_path: ELF file to inspect.

    Returns:
        Flat list of parsed symbols.
    """
    data = elf_path.read_bytes()
    header = ELF_HEADER_FORMAT.unpack_from(data, 0)
    e_shoff = header[6]
    e_shentsize = header[11]
    e_shnum = header[12]

    if e_shoff == 0 or e_shnum == 0:
        return [], []
    if e_shentsize != SECTION_HEADER_FORMAT.size:
        raise RuntimeError("unexpected ELF section header size")

    sections = []
    for index in range(e_shnum):
        offset = e_shoff + index * e_shentsize
        sections.append(SECTION_HEADER_FORMAT.unpack_from(data, offset))

    symbols: List[RawSymbolInfo] = []
    for section in sections:
        sh_type = section[1]
        sh_offset = section[4]
        sh_size = section[5]
        sh_link = section[6]
        sh_entsize = section[9]

        if sh_type not in (SHT_SYMTAB, SHT_DYNSYM) or sh_entsize != SYMBOL_FORMAT.size:
            continue
        if sh_link >= len(sections):
            raise RuntimeError("ELF symbol table string section is invalid")

        strtab = sections[sh_link]
        strtab_offset = strtab[4]
        strtab_size = strtab[5]
        if strtab_offset + strtab_size > len(data):
            raise RuntimeError("ELF string table is truncated")

        strings = data[strtab_offset : strtab_offset + strtab_size]
        count = sh_size // sh_entsize
        for index in range(count):
            sym_offset = sh_offset + index * sh_entsize
            st_name, st_info, st_other, st_shndx, st_value, st_size = SYMBOL_FORMAT.unpack_from(data, sym_offset)
            symbols.append(
                RawSymbolInfo(
                    name=read_c_string(strings, st_name),
                    info=st_info,
                    section_index=st_shndx,
                    value=st_value,
                )
            )

    return symbols


def find_elf_symbol_value(elf_path: Path, symbol_name: str) -> int | None:
    """
    Return the value of one named ELF symbol when present.

    Args:
        elf_path: ELF file to inspect.
        symbol_name: Exact symbol to find.

    Returns:
        Symbol value, or None when the symbol is absent.
    """
    for symbol in load_elf_symbols(elf_path):
        if symbol.name == symbol_name:
            return symbol.value
    return None


def synthesize_exports_from_elf(elf_path: Path) -> List[Dict[str, str]]:
    """
    Synthesize DLL exports from eligible global ELF symbols.

    This preserves the current default DLL workflow while the tree is migrated
    away from implicit symbol export rules.

    Args:
        elf_path: Linked shared ELF image.

    Returns:
        Export specs in the same shape as inline metadata.
    """
    exports: List[Dict[str, str]] = []
    seen_names: set[str] = set()

    for symbol in load_elf_symbols(elf_path):
        binding = symbol.info >> 4
        symbol_type = symbol.info & 0xF

        if not symbol.name or symbol.name == "_shared_library_entry":
            continue
        if symbol.name.startswith("_") or symbol.name.startswith("."):
            continue
        if binding not in (STB_GLOBAL, STB_WEAK):
            continue
        if symbol_type not in (STT_FUNC, STT_OBJECT):
            continue
        if symbol.section_index in (SHN_UNDEF, SHN_ABS):
            continue
        if symbol.name in seen_names:
            continue

        exports.append({"name": symbol.name, "symbol": symbol.name})
        seen_names.add(symbol.name)

    return exports


def synthesize_exports_from_objects(object_paths: List[Path]) -> List[Dict[str, str]]:
    """
    Synthesize exported symbols from one or more ELF object files.

    Args:
        object_paths: Compiled ELF object paths.

    Returns:
        De-duplicated export specs suitable for link preservation and packing.
    """
    exports: List[Dict[str, str]] = []
    seen_names: set[str] = set()

    for object_path in object_paths:
        for export_spec in synthesize_exports_from_elf(object_path):
            symbol_name = str(export_spec.get("symbol", "")).strip()
            if not symbol_name or symbol_name in seen_names:
                continue
            exports.append(export_spec)
            seen_names.add(symbol_name)

    return exports


def parse_elf_metadata(elf_path: Path) -> Tuple[List[SectionInfo], int, Dict[str, SymbolInfo], bool, Dict[str, bytes]]:
    """
    Parse minimal ELF64 metadata directly from file bytes.

    Args:
        elf_path: Path to ELF executable or relocatable object.

    Returns:
        Tuple of sections, entry address, symbol map, relocatable flag, and raw section payloads.
    """
    data = elf_path.read_bytes()
    symbols: Dict[str, SymbolInfo] = {}
    sections: List[SectionInfo] = []
    raw_sections: Dict[str, bytes] = {}

    if len(data) < 64 or data[:4] != b"\x7fELF":
        raise RuntimeError(f"not an ELF file: {elf_path}")
    if data[4] != 2 or data[5] != 1:
        raise RuntimeError("expected ELF64 little-endian input")

    ehdr = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
    e_type = ehdr[1]
    e_entry = ehdr[4]
    e_shoff = ehdr[6]
    e_shentsize = ehdr[11]
    e_shnum = ehdr[12]
    e_shstrndx = ehdr[13]
    relocatable = (e_type == ELF_TYPE_REL)

    if e_shoff == 0 or e_shnum == 0:
        raise RuntimeError("ELF has no section table")

    shdrs = []
    for index in range(e_shnum):
        off = e_shoff + index * e_shentsize
        shdrs.append(SECTION_HEADER_FORMAT.unpack_from(data, off))

    shstr_hdr = shdrs[e_shstrndx]
    shstr = data[shstr_hdr[4] : shstr_hdr[4] + shstr_hdr[5]]

    def c_string(blob: bytes, off: int) -> str:
        end = blob.find(b"\x00", off)
        if end < 0:
            raise RuntimeError("unterminated ELF string table entry")
        return blob[off:end].decode("utf-8")

    # First pass: collect allocatable sections with raw ELF offsets and flags.
    for idx, sh in enumerate(shdrs):
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = sh
        name = c_string(shstr, sh_name) if sh_name < len(shstr) else ""

        if sh_type != 8 and sh_size != 0:
            raw_sections[name] = data[sh_offset : sh_offset + sh_size]

        if not (sh_flags & 0x2) or sh_size == 0:
            continue

        sec_flags = 0
        sec_flags |= 0x1  # alloc sections are always readable in MVP format
        if sh_flags & 0x1:
            sec_flags |= 0x2
        if sh_flags & 0x4:
            sec_flags |= 0x4
        if sh_type == 8:
            sec_flags |= 0x8

        file_size = 0 if sh_type == 8 else sh_size
        sections.append(
            SectionInfo(
                name=name,
                flags=sec_flags,
                rva=sh_addr,
                file_off=sh_offset,
                file_size=file_size,
                virt_size=sh_size,
                align=max(1, sh_addralign),
                section_index=idx,
            )
        )

    # Second pass: parse symbol tables so fallback mode can find entry symbol.
    for idx, sh in enumerate(shdrs):
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = sh
        if sh_type != SHT_SYMTAB or sh_entsize == 0:
            continue

        strtab_hdr = shdrs[sh_link]
        strtab = data[strtab_hdr[4] : strtab_hdr[4] + strtab_hdr[5]]
        count = sh_size // sh_entsize

        for sym_index in range(count):
            sym_off = sh_offset + sym_index * sh_entsize
            st_name, st_info, st_other, st_shndx, st_value, st_size = SYMBOL_FORMAT.unpack_from(data, sym_off)
            if st_name == 0 or st_shndx == 0:
                continue
            sym_name = c_string(strtab, st_name)
            symbols[sym_name] = SymbolInfo(name=sym_name, value=st_value, section_index=st_shndx)

    return sections, e_entry, symbols, relocatable, raw_sections


def extract_kernel_module_metadata_from_elf(elf_path: Path) -> Dict[str, Any] | None:
    """
    Extract embedded legacy kernel-module metadata from one ELF image.

    Args:
        elf_path: ELF file that may carry `.ros.module.meta`.

    Returns:
        Decoded metadata dictionary, or None when the section is absent.
    """
    _sections, _entry, _symbols, _relocatable, raw_sections = parse_elf_metadata(elf_path)
    blob = raw_sections.get(KERNEL_MODULE_METADATA_SECTION, b"")
    expected_size = KERNEL_MODULE_METADATA_HEADER_FORMAT.size + (KERNEL_MODULE_EXPORT_MAX * KERNEL_MODULE_METADATA_EXPORT_FORMAT.size)

    if not blob:
        return None
    if len(blob) < expected_size:
        raise RuntimeError(f"{elf_path.name}: embedded kernel-module metadata is truncated")

    header = KERNEL_MODULE_METADATA_HEADER_FORMAT.unpack_from(blob, 0)
    magic, version, flags, raw_name, raw_init, raw_shutdown, raw_idle, export_count = header
    if magic != KERNEL_MODULE_METADATA_MAGIC:
        raise RuntimeError(f"{elf_path.name}: embedded kernel-module metadata magic is invalid")
    if version != KERNEL_MODULE_METADATA_VERSION:
        raise RuntimeError(f"{elf_path.name}: unsupported kernel-module metadata version {version}")
    if export_count > KERNEL_MODULE_EXPORT_MAX:
        raise RuntimeError(f"{elf_path.name}: embedded kernel-module metadata exceeds export limit")

    exports: List[Dict[str, str]] = []
    cursor = KERNEL_MODULE_METADATA_HEADER_FORMAT.size
    for index in range(KERNEL_MODULE_EXPORT_MAX):
        export_name_raw, export_symbol_raw = KERNEL_MODULE_METADATA_EXPORT_FORMAT.unpack_from(blob, cursor)
        cursor += KERNEL_MODULE_METADATA_EXPORT_FORMAT.size
        if index >= export_count:
            continue
        export_name = decode_fixed_ascii(export_name_raw)
        export_symbol = decode_fixed_ascii(export_symbol_raw)
        if not export_name or not export_symbol:
            raise RuntimeError(f"{elf_path.name}: embedded kernel-module export {index} is incomplete")
        exports.append({"name": export_name, "symbol": export_symbol})

    return {
        "name": decode_fixed_ascii(raw_name),
        "init": decode_fixed_ascii(raw_init),
        "shutdown": decode_fixed_ascii(raw_shutdown),
        "idle": decode_fixed_ascii(raw_idle),
        "flags": flags,
        "exports": exports,
    }


def synthesize_sys_exports_from_metadata(elf_path: Path, metadata: Dict[str, Any]) -> List[Dict[str, str]]:
    """
    Convert embedded kernel-module metadata into DLL0 exports.

    Args:
        elf_path: ELF file used only for diagnostics.
        metadata: Parsed embedded module metadata.

    Returns:
        Export specs suitable for `pack_image`.
    """
    exports = list(metadata.get("exports", []))

    init_symbol = str(metadata.get("init", "")).strip()
    shutdown_symbol = str(metadata.get("shutdown", "")).strip()
    idle_symbol = str(metadata.get("idle", "")).strip()

    if not init_symbol:
        raise RuntimeError(f"{elf_path.name}: embedded kernel-module metadata requires a non-empty init symbol")

    exports.append({"name": KERNEL_MODULE_INIT_EXPORT, "symbol": init_symbol})
    if shutdown_symbol:
        exports.append({"name": KERNEL_MODULE_SHUTDOWN_EXPORT, "symbol": shutdown_symbol})
    if idle_symbol:
        exports.append({"name": KERNEL_MODULE_IDLE_EXPORT, "symbol": idle_symbol})
    return exports


def extract_relocations_from_elf(
    elf_path: Path,
    alloc_sections: List[SectionInfo],
    section_rva_by_index: Dict[int, int],
    min_rva: int,
    image_size: int,
    relocatable: bool,
) -> Tuple[List[RelocationInfo], List[Dict[str, Any]]]:
    """
    Extract supported AArch64 RELA records and translate them into DLL0 reloc entries.

    Args:
        elf_path: ELF executable, shared object, or relocatable object.
        alloc_sections: Allocatable sections included in the packed image.
        section_rva_by_index: Rebasing map for packed sections.
        min_rva: Lowest input section RVA.
        image_size: Total packed image size.
        relocatable: Whether the input ELF is ET_REL.

    Returns:
        Tuple of relocations and synthesized absolute import slots.
    """
    data = elf_path.read_bytes()
    ehdr = ELF_HEADER_FORMAT.unpack_from(data, 0)
    e_shoff = ehdr[6]
    e_shentsize = ehdr[11]
    e_shnum = ehdr[12]

    if e_shoff == 0 or e_shnum == 0:
        return []
    if e_shentsize != SECTION_HEADER_FORMAT.size:
        raise RuntimeError("unexpected ELF section header size")

    shdrs = []
    for index in range(e_shnum):
        off = e_shoff + index * e_shentsize
        shdrs.append(SECTION_HEADER_FORMAT.unpack_from(data, off))

    section_by_index: Dict[int, SectionInfo] = {section.section_index: section for section in alloc_sections}
    relocs: List[RelocationInfo] = []
    imports: List[Dict[str, Any]] = []

    def resolve_loaded_address_rva(address: int) -> int:
        for section in alloc_sections:
            start = section.rva
            end = section.rva + section.virt_size
            if address >= start and address < end:
                return (section.rva - min_rva) + (address - start)
        raise RuntimeError(f"address 0x{address:x} is outside alloc sections")

    def resolve_symbol_rva(symbol_value: int, section_index: int) -> int:
        if section_index == SHN_ABS:
            raise RuntimeError("absolute-symbol relocations are not supported in DLL0 images")
        if relocatable:
            if section_index not in section_rva_by_index:
                raise RuntimeError(f"relocation references unmapped section index {section_index}")
            return section_rva_by_index[section_index] + symbol_value
        if section_index in section_by_index:
            section = section_by_index[section_index]
            if symbol_value >= section.rva and symbol_value < section.rva + section.virt_size:
                return (section.rva - min_rva) + (symbol_value - section.rva)
        return resolve_loaded_address_rva(symbol_value)

    for sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize in shdrs:
        if sh_type != SHT_RELA or sh_size == 0:
            continue
        if sh_entsize != RELA_FORMAT.size:
            raise RuntimeError("unexpected ELF rela entry size")
        if sh_link >= len(shdrs):
            raise RuntimeError("ELF relocation symbol table link is invalid")
        if sh_offset + sh_size > len(data):
            raise RuntimeError("ELF relocation table is truncated")

        symtab_hdr = shdrs[sh_link]
        symtab_type = symtab_hdr[1]
        symtab_offset = symtab_hdr[4]
        symtab_size = symtab_hdr[5]
        symtab_strtab_index = symtab_hdr[6]
        symtab_entsize = symtab_hdr[9]

        if symtab_type not in (SHT_SYMTAB, SHT_DYNSYM) or symtab_entsize != SYMBOL_FORMAT.size:
            raise RuntimeError("ELF relocation section references an unsupported symbol table")
        if symtab_strtab_index >= len(shdrs):
            raise RuntimeError("ELF symbol table string section is invalid")
        if symtab_offset + symtab_size > len(data):
            raise RuntimeError("ELF symbol table is truncated")

        strtab_hdr = shdrs[symtab_strtab_index]
        strtab_offset = strtab_hdr[4]
        strtab_size = strtab_hdr[5]
        if strtab_offset + strtab_size > len(data):
            raise RuntimeError("ELF symbol string table is truncated")
        strings = data[strtab_offset : strtab_offset + strtab_size]

        symbol_count = symtab_size // symtab_entsize
        symbols = [SYMBOL_FORMAT.unpack_from(data, symtab_offset + i * symtab_entsize) for i in range(symbol_count)]

        for index in range(sh_size // sh_entsize):
            rela_offset = sh_offset + index * sh_entsize
            r_offset, r_info, r_addend = RELA_FORMAT.unpack_from(data, rela_offset)
            r_type = r_info & 0xFFFFFFFF
            r_sym = r_info >> 32

            if r_type == 0:
                continue

            if relocatable:
                if sh_info not in section_rva_by_index:
                    continue
                patch_rva = section_rva_by_index[sh_info] + r_offset
            else:
                patch_rva = resolve_loaded_address_rva(r_offset)

            if patch_rva + 8 > image_size:
                raise RuntimeError(f"relocation patch offset outside image: 0x{patch_rva:x}")

            if r_type == R_AARCH64_RELATIVE:
                target_rva = resolve_loaded_address_rva(r_addend)
                relocs.append(RelocationInfo(patch_rva=patch_rva, type=1, addend=target_rva))
                continue

            if r_sym >= len(symbols):
                raise RuntimeError(f"relocation references invalid symbol index {r_sym}")

            st_name, st_info, st_other, st_shndx, st_value, st_size = symbols[r_sym]
            symbol_name = read_c_string(strings, st_name)
            if st_shndx == SHN_UNDEF:
                if r_type in (R_AARCH64_ABS64, R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT):
                    if (st_info >> 4) == STB_WEAK:
                        continue
                    if not symbol_name:
                        raise RuntimeError("undefined import relocation has no symbol name")
                    imports.append({
                        "module": "kernel",
                        "symbol": symbol_name,
                        "iat_rva": patch_rva,
                    })
                    continue
                if r_type == R_AARCH64_PREL64:
                    raise RuntimeError(f"unsupported relative import relocation for symbol: {symbol_name}")
                continue

            target_rva = resolve_symbol_rva(st_value, st_shndx) + r_addend
            if target_rva < 0 or target_rva >= image_size:
                raise RuntimeError(f"relocation target outside image: 0x{target_rva:x}")

            if r_type in (R_AARCH64_ABS64, R_AARCH64_GLOB_DAT, R_AARCH64_JUMP_SLOT):
                reloc_type = 1
            elif r_type == R_AARCH64_PREL64:
                reloc_type = 2
            else:
                raise RuntimeError(f"unsupported AArch64 relocation type: {r_type}")

            relocs.append(RelocationInfo(patch_rva=patch_rva, type=reloc_type, addend=target_rva))

    return relocs, imports


def extract_inline_metadata_from_elf(elf_path: Path) -> Tuple[List[Dict[str, str]], List[Dict[str, str]]]:
    """
    Extract my-loader inline import/export declarations from one ELF file.

    Args:
        elf_path: ELF object or image that may carry `.ldrmeta.*` sections.

    Returns:
        Tuple(import_specs, export_specs).
    """
    _sections, _entry, symbols, _relocatable, raw_sections = parse_elf_metadata(elf_path)
    imports: List[Dict[str, str]] = []
    exports: List[Dict[str, str]] = []

    import_blob = raw_sections.get(LDR_META_IMPORT_SECTION, b"")
    if import_blob:
        if len(import_blob) % LDR_IMPORT_META_FORMAT.size != 0:
            raise RuntimeError(f"{elf_path.name}: inline import section has invalid size")
        for index in range(len(import_blob) // LDR_IMPORT_META_FORMAT.size):
            offset = index * LDR_IMPORT_META_FORMAT.size
            version, kind, module_raw, symbol_raw, slot_raw = LDR_IMPORT_META_FORMAT.unpack_from(import_blob, offset)
            source = f"{elf_path.name} inline import"
            if version != LDR_META_VERSION or kind != LDR_META_KIND_IMPORT:
                raise RuntimeError(f"{source}[{index}] has unsupported version or kind")
            spec = {
                "module": read_meta_string(module_raw, "module", f"{source}[{index}]"),
                "symbol": read_meta_string(symbol_raw, "symbol", f"{source}[{index}]"),
                "slot_symbol": read_meta_string(slot_raw, "slot_symbol", f"{source}[{index}]"),
            }
            module_name, symbol_name, slot_symbol = normalize_import_spec(index, spec, source=source)
            if slot_symbol not in symbols:
                raise RuntimeError(f"{source}[{index}] unresolved slot symbol: {slot_symbol}")
            imports.append({
                "module": module_name,
                "symbol": symbol_name,
                "slot_symbol": slot_symbol,
            })

    export_blob = raw_sections.get(LDR_META_EXPORT_SECTION, b"")
    if export_blob:
        source = f"{elf_path.name} inline export"
        if len(export_blob) % LDR_EXPORT_META_FORMAT.size == 0:
            for index in range(len(export_blob) // LDR_EXPORT_META_FORMAT.size):
                offset = index * LDR_EXPORT_META_FORMAT.size
                version, kind, export_name_raw, symbol_raw = LDR_EXPORT_META_FORMAT.unpack_from(export_blob, offset)
                if version != LDR_META_VERSION or kind != LDR_META_KIND_EXPORT:
                    raise RuntimeError(f"{source}[{index}] has unsupported version or kind")

                spec = {
                    "name": read_meta_string(export_name_raw, "name", f"{source}[{index}]"),
                    "symbol": read_meta_string(symbol_raw, "symbol", f"{source}[{index}]"),
                }
                export_name, export_symbol = normalize_export_spec(index, spec, source=source)
                if export_symbol not in symbols:
                    raise RuntimeError(f"{source}[{index}] unresolved export symbol: {export_symbol}")
                exports.append({
                    "name": export_name,
                    "symbol": export_symbol,
                })
        elif len(export_blob) % LDR_EXPORT_META_V1_FORMAT.size == 0:
            for index in range(len(export_blob) // LDR_EXPORT_META_V1_FORMAT.size):
                offset = index * LDR_EXPORT_META_V1_FORMAT.size
                version, kind, symbol_raw = LDR_EXPORT_META_V1_FORMAT.unpack_from(export_blob, offset)
                if version not in (1, LDR_META_VERSION) or kind != LDR_META_KIND_EXPORT:
                    raise RuntimeError(f"{source}[{index}] has unsupported legacy version or kind")

                spec = {
                    "symbol": read_meta_string(symbol_raw, "symbol", f"{source}[{index}]"),
                }
                export_name, export_symbol = normalize_export_spec(index, spec, source=source)
                if export_symbol not in symbols:
                    raise RuntimeError(f"{source}[{index}] unresolved export symbol: {export_symbol}")
                exports.append({
                    "name": export_name,
                    "symbol": export_symbol,
                })
        else:
            raise RuntimeError(f"{elf_path.name}: inline export section has invalid size")

    return imports, exports


def apply_inline_metadata(cfg: BuildConfig, objects: List[Path]) -> None:
    """
    Replace JSON metadata with inline ELF metadata when present in compiled objects.

    Args:
        cfg: Build configuration updated in place.
        objects: Compiled ELF object files.

    Returns:
        None.
    """
    inline_imports: List[Dict[str, str]] = []
    inline_exports: List[Dict[str, str]] = []

    for obj in objects:
        obj_imports, obj_exports = extract_inline_metadata_from_elf(obj)
        inline_imports.extend(obj_imports)
        inline_exports.extend(obj_exports)

    if inline_imports:
        print(f"[meta] using {len(inline_imports)} inline imports from ELF objects")
        cfg.imports = inline_imports
    if inline_exports:
        print(f"[meta] using {len(inline_exports)} inline exports from ELF objects")
        cfg.exports = inline_exports


def collect_metadata(elf_path: Path) -> Tuple[List[SectionInfo], int]:
    """
    Collect section metadata and entry point from ELF.

    Args:
        elf_path: Linked ELF path.

    Returns:
        Tuple(sections, entry_address).
    """
    sections, entry, _symbols, _relocatable, _raw_sections = parse_elf_metadata(elf_path)
    return sections, entry


def pack_image(
    cfg: BuildConfig,
    elf_path: Path,
    sections: List[SectionInfo],
    entry_addr: int,
    out_file: Path,
    entry_symbol: str,
) -> None:
    """
    Pack one Windows-like DLL0 image from ELF metadata and section payloads.

    Args:
        cfg: Build recipe.
        elf_path: Input ELF file.
        sections: Parsed section metadata.
        entry_addr: Absolute entry address from ELF header.
        out_file: Output `.exe/.dll/.sys` path.

    Returns:
        None. Writes packed output and sidecar manifest.
    """
    elf_data = elf_path.read_bytes()
    _parsed_sections, parsed_entry, symbols, relocatable, _raw_sections = parse_elf_metadata(elf_path)

    alloc_sections = [s for s in sections if s.virt_size != 0]

    if not alloc_sections and relocatable:
        alloc_sections = [s for s in _parsed_sections if s.virt_size != 0]

    if relocatable:
        next_rva = 0
        for sec in sorted(alloc_sections, key=lambda s: (s.file_off, s.section_index)):
            sec_align = max(sec.align, DLL_SECTION_ALIGNMENT)
            next_rva = align_up(next_rva, sec_align)
            sec.rva = next_rva
            next_rva += align_up(sec.virt_size, DLL_SECTION_ALIGNMENT)

    if not alloc_sections:
        raise RuntimeError(f"no allocatable sections found in ELF: {elf_path}")

    alloc_sections.sort(key=lambda s: s.rva)

    min_rva = min((s.rva for s in alloc_sections), default=0)
    # Keep the packed image page-relative layout aligned with the ELF view so
    # AArch64 ADRP/ADD pairs continue to resolve nearby symbols after packing.
    # Using the exact first-section RVA can introduce a sub-page shift for DLLs
    # that start with `.hash`/`.dynsym` metadata, which corrupts intra-image
    # references even though every section payload is copied correctly.
    rva_bias = align_down(min_rva, DLL_SECTION_ALIGNMENT)
    image_span = max((s.rva + s.virt_size for s in alloc_sections), default=0) - rva_bias
    packed_sections: List[Dict[str, Any]] = []
    section_by_index: Dict[int, SectionInfo] = {}

    for sec in alloc_sections:
        packed_sections.append(
            {
                "info": sec,
                "packed_rva": sec.rva - rva_bias,
                "payload": bytearray(elf_data[sec.file_off : sec.file_off + sec.file_size] if sec.file_size else b""),
            }
        )
        section_by_index[sec.section_index] = sec

    string_pool = bytearray()
    name_offsets: Dict[str, int] = {}

    def add_str(value: str) -> int:
        if value in name_offsets:
            return name_offsets[value]
        off = len(string_pool)
        string_pool.extend(value.encode("utf-8") + b"\x00")
        name_offsets[value] = off
        return off

    section_rva_by_index: Dict[int, int] = {section.section_index: (section.rva - rva_bias) for section in alloc_sections}

    entry_rva = 0

    def resolve_symbol_rva(symbol_name: str) -> int:
        sym = symbols.get(symbol_name)
        if not sym:
            raise RuntimeError(f"symbol not found in ELF: {symbol_name}")
        if relocatable:
            if sym.section_index not in section_rva_by_index:
                raise RuntimeError(f"symbol section not mapped in image: {symbol_name}")
            return section_rva_by_index[sym.section_index] + sym.value
        if sym.value >= rva_bias and sym.value < (rva_bias + image_span):
            return sym.value - rva_bias
        if sym.section_index in section_by_index:
            section = section_by_index[sym.section_index]
            if sym.value >= section.rva and sym.value < section.rva + section.virt_size:
                return (section.rva - rva_bias) + (sym.value - section.rva)
        raise RuntimeError(f"cannot resolve RVA for symbol: {symbol_name}")

    if entry_symbol in symbols:
        entry_rva = resolve_symbol_rva(entry_symbol)
    elif parsed_entry >= rva_bias and parsed_entry < (rva_bias + image_span) and parsed_entry != 0:
        entry_rva = parsed_entry - rva_bias

    if entry_rva >= image_span:
        raise RuntimeError(f"entry symbol resolves outside packed image span: {entry_symbol}")

    import_entries: List[Dict[str, Any]] = []
    for i, raw_import in enumerate(cfg.imports):
        module_name, symbol_name, slot_symbol = normalize_import_spec(i, raw_import)
        module_off = add_str(module_name)
        symbol_off = add_str(symbol_name)
        iat_rva = resolve_symbol_rva(slot_symbol)
        if iat_rva + 8 > image_span:
            raise RuntimeError(f"import slot outside image for symbol: {slot_symbol}")
        import_entries.append(
            {
                "module": module_name,
                "symbol": symbol_name,
                "module_off": module_off,
                "symbol_off": symbol_off,
                "iat_rva": iat_rva,
            }
        )

    export_entries: List[Dict[str, Any]] = []
    for i, raw_export in enumerate(cfg.exports):
        export_name, export_symbol = normalize_export_spec(i, raw_export)
        symbol_off = add_str(export_name)
        symbol_rva = resolve_symbol_rva(export_symbol)
        if symbol_rva >= image_span:
            raise RuntimeError(f"export symbol outside image for symbol: {export_symbol}")
        export_entries.append(
            {
                "name": export_name,
                "symbol_off": symbol_off,
                "symbol_rva": symbol_rva,
            }
        )

    reloc_entries, synthesized_imports = extract_relocations_from_elf(
        elf_path,
        alloc_sections,
        section_rva_by_index,
        rva_bias,
        image_span,
        relocatable,
    )
    for synthesized_import in synthesized_imports:
        module_off = add_str(str(synthesized_import["module"]))
        symbol_off = add_str(str(synthesized_import["symbol"]))
        iat_rva = int(synthesized_import["iat_rva"])
        if iat_rva + 8 > image_span:
            raise RuntimeError(f"import slot outside image for symbol: {synthesized_import['symbol']}")
        import_entries.append(
            {
                "module": str(synthesized_import["module"]),
                "symbol": str(synthesized_import["symbol"]),
                "module_off": module_off,
                "symbol_off": symbol_off,
                "iat_rva": iat_rva,
            }
        )

    grouped_imports: List[Dict[str, Any]] = []
    grouped_import_lookup: Dict[int, int] = {}
    import_symbols: List[Dict[str, Any]] = []
    for import_entry in import_entries:
        module_key = int(import_entry["module_off"])
        if module_key not in grouped_import_lookup:
            grouped_import_lookup[module_key] = len(grouped_imports)
            grouped_imports.append(
                {
                    "module": import_entry["module"],
                    "module_off": module_key,
                    "first_symbol_index": len(import_symbols),
                    "symbol_count": 0,
                }
            )
        import_symbols.append(
            {
                "symbol": import_entry["symbol"],
                "symbol_off": int(import_entry["symbol_off"]),
                "iat_rva": int(import_entry["iat_rva"]),
            }
        )
        grouped_imports[grouped_import_lookup[module_key]]["symbol_count"] += 1

    preferred_base = DLL_DEFAULT_EXE_BASE
    if cfg.kind == "dll":
        preferred_base = DLL_DEFAULT_DLL_BASE
    elif cfg.kind == "sys":
        preferred_base = DLL_DEFAULT_SYS_BASE

    abs_reloc_count = 0
    for reloc in reloc_entries:
        if reloc.type == 1:
            abs_reloc_count += 1
        elif reloc.type != 2:
            raise RuntimeError(f"unsupported packed relocation type: {reloc.type}")

    header_size_unaligned = (
        DLL_HEADER_FORMAT.size
        + (len(packed_sections) * DLL_SECTION_FORMAT.size)
        + (len(grouped_imports) * DLL_IMPORT_MODULE_FORMAT.size)
        + (len(import_symbols) * DLL_IMPORT_SYMBOL_FORMAT.size)
        + (len(export_entries) * DLL_EXPORT_FORMAT.size)
        + (abs_reloc_count * DLL_RELOCATION_FORMAT.size)
        + len(string_pool)
    )
    header_size = align_up(header_size_unaligned, DLL_SECTION_ALIGNMENT)
    rva_shift = header_size
    image_size = align_up(rva_shift + image_span, DLL_SECTION_ALIGNMENT)
    if entry_rva != 0:
        entry_rva += rva_shift

    def patch_section_u64(patch_rva: int, value: int) -> None:
        for packed_section in packed_sections:
            start = int(packed_section["packed_rva"])
            payload = packed_section["payload"]
            end = start + len(payload)

            if patch_rva >= start and patch_rva + 8 <= end:
                struct.pack_into("<Q", payload, patch_rva - start, value & 0xFFFFFFFFFFFFFFFF)
                return
        raise RuntimeError(f"relocation patch falls outside raw section data: 0x{patch_rva:x}")

    def patch_section_i64(patch_rva: int, value: int) -> None:
        for packed_section in packed_sections:
            start = int(packed_section["packed_rva"])
            payload = packed_section["payload"]
            end = start + len(payload)

            if patch_rva >= start and patch_rva + 8 <= end:
                struct.pack_into("<q", payload, patch_rva - start, value)
                return
        raise RuntimeError(f"relocation patch falls outside raw section data: 0x{patch_rva:x}")

    for import_symbol in import_symbols:
        patch_section_u64(int(import_symbol["iat_rva"]), 0)

    final_relocations: List[Dict[str, int]] = []
    for reloc in reloc_entries:
        shifted_patch_rva = rva_shift + reloc.patch_rva
        shifted_target_rva = rva_shift + reloc.addend

        if reloc.type == 1:
            patch_section_u64(reloc.patch_rva, preferred_base + shifted_target_rva)
            final_relocations.append({"type": DLL_RELOC_ABS64, "target_rva": shifted_patch_rva})
            continue

        patch_section_i64(reloc.patch_rva, shifted_target_rva - shifted_patch_rva)

    section_table_offset = DLL_HEADER_FORMAT.size
    import_module_table_offset = section_table_offset + (len(packed_sections) * DLL_SECTION_FORMAT.size)
    import_symbol_table_offset = import_module_table_offset + (len(grouped_imports) * DLL_IMPORT_MODULE_FORMAT.size)
    export_table_offset = import_symbol_table_offset + (len(import_symbols) * DLL_IMPORT_SYMBOL_FORMAT.size)
    relocation_table_offset = export_table_offset + (len(export_entries) * DLL_EXPORT_FORMAT.size)
    string_table_offset = relocation_table_offset + (len(final_relocations) * DLL_RELOCATION_FORMAT.size)

    section_table = bytearray()
    payload = bytearray()
    current_file_offset = header_size
    manifest_sections: List[Dict[str, Any]] = []

    for packed_section in packed_sections:
        section_info = packed_section["info"]
        section_payload = packed_section["payload"]
        section_name = section_info.name.encode("ascii", errors="ignore")[:8].ljust(8, b"\x00")

        current_file_offset = align_up(current_file_offset, DLL_FILE_ALIGNMENT)
        while header_size + len(payload) < current_file_offset:
            payload.extend(b"\x00")

        section_table.extend(
            DLL_SECTION_FORMAT.pack(
                section_name,
                rva_shift + int(packed_section["packed_rva"]),
                section_info.virt_size,
                current_file_offset,
                len(section_payload),
                section_info.flags,
                0,
            )
        )
        manifest_sections.append(
            {
                "name": section_info.name,
                "rva": rva_shift + int(packed_section["packed_rva"]),
                "file_size": len(section_payload),
                "virt_size": section_info.virt_size,
                "flags": section_info.flags,
            }
        )
        payload.extend(section_payload)
        current_file_offset += len(section_payload)

    import_module_table = bytearray()
    for import_module in grouped_imports:
        import_module_table.extend(
            DLL_IMPORT_MODULE_FORMAT.pack(
                int(import_module["module_off"]),
                int(import_module["first_symbol_index"]),
                int(import_module["symbol_count"]),
                0,
            )
        )

    import_symbol_table = bytearray()
    for import_symbol in import_symbols:
        import_symbol_table.extend(
            DLL_IMPORT_SYMBOL_FORMAT.pack(
                int(import_symbol["symbol_off"]),
                rva_shift + int(import_symbol["iat_rva"]),
                0,
                0,
            )
        )

    export_table = bytearray()
    for export_entry in export_entries:
        export_table.extend(
            DLL_EXPORT_FORMAT.pack(
                int(export_entry["symbol_off"]),
                0,
                rva_shift + int(export_entry["symbol_rva"]),
            )
        )

    relocation_table = bytearray()
    for relocation in final_relocations:
        relocation_table.extend(
            DLL_RELOCATION_FORMAT.pack(
                int(relocation["type"]),
                0,
                int(relocation["target_rva"]),
            )
        )

    header = DLL_HEADER_FORMAT.pack(
        MAGIC,
        VERSION_MAJOR,
        VERSION_MINOR,
        DLL_MACHINE_AARCH64,
        KIND_MAP[cfg.kind],
        0,
        preferred_base,
        entry_rva,
        image_size,
        header_size,
        DLL_SECTION_ALIGNMENT,
        DLL_FILE_ALIGNMENT,
        len(packed_sections),
        section_table_offset,
        import_module_table_offset,
        len(grouped_imports),
        import_symbol_table_offset,
        len(import_symbols),
        export_table_offset,
        len(export_entries),
        relocation_table_offset,
        len(final_relocations),
        string_table_offset,
        len(string_pool),
        0,
        0,
    )

    blob = bytearray()
    blob.extend(header)
    blob.extend(section_table)
    blob.extend(import_module_table)
    blob.extend(import_symbol_table)
    blob.extend(export_table)
    blob.extend(relocation_table)
    blob.extend(string_pool)
    if len(blob) < header_size:
        blob.extend(b"\x00" * (header_size - len(blob)))
    blob.extend(payload)
    write_bytes_atomic(out_file, bytes(blob))

    manifest = {
        "name": cfg.name,
        "kind": cfg.kind,
        "image_base": preferred_base,
        "header_size": header_size,
        "entry_rva": entry_rva,
        "image_size": image_size,
        "sections": manifest_sections,
        "imports": [
            {
                "module": str(import_entry["module"]),
                "symbol": str(import_entry["symbol"]),
                "iat_rva": rva_shift + int(import_entry["iat_rva"]),
            }
            for import_entry in import_entries
        ],
        "relocations": [
            {
                "target_rva": int(relocation["target_rva"]),
                "type": int(relocation["type"]),
            }
            for relocation in final_relocations
        ],
        "exports": [
            {
                "symbol": str(export_entry["name"]),
                "symbol_rva": rva_shift + int(export_entry["symbol_rva"]),
            }
            for export_entry in export_entries
        ],
    }
    out_file.with_suffix(out_file.suffix + ".manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


def load_kernel_segment(data: bytes) -> Dict[str, Any]:
    """
    Parse kernel ELF and merge all PT_LOAD segments into one packed kernel-image payload.
    """
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

    load_segments: List[Dict[str, Any]] = []
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


def build_kernel_image(kernel_segment: Dict[str, Any]) -> bytes:
    """
    Serialize one kernel ELF load segment into the boot kernel-image format.
    """
    header_size = KERNEL_IMAGE_HEADER_FORMAT.size
    header = KERNEL_IMAGE_HEADER_FORMAT.pack(
        KERNEL_IMAGE_MAGIC,
        KERNEL_IMAGE_VERSION,
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


def pack_kernel_image(input_path: Path, output_path: Path) -> None:
    """
    Pack the linked kernel ELF into the boot kernel-image format.
    """
    kernel_segment = load_kernel_segment(input_path.read_bytes())
    real_load = find_elf_symbol_value(input_path, "real_load")

    if real_load is not None:
        kernel_segment["paddr"] = real_load

    image = build_kernel_image(kernel_segment)
    write_bytes_atomic(output_path, image)


def build_pack_only_config(args: argparse.Namespace) -> BuildConfig:
    """
    Build a synthetic config for pack-existing-ELF mode.

    Args:
        args: Parsed argparse namespace.

    Returns:
        BuildConfig compatible with pack_image.
    """
    kind = str(args.kind or "").lower()
    if kind not in KIND_MAP:
        raise ValueError("--pack-elf mode requires --kind exe|dll|sys")
    if not args.input or not args.output:
        raise ValueError("--pack-elf mode requires --input and --output")

    name = str(args.name or Path(args.output).stem).strip()
    if not name:
        raise ValueError("--pack-elf mode requires a non-empty --name or output stem")

    return BuildConfig(
        name=name,
        kind=kind,
        sources=[],
        include_dirs=[],
        defines=[],
        cflags=[],
        ldflags=[],
        entry_symbol=str(args.entry_symbol or "Init").strip() or "Init",
        imports=[],
        exports=[],
        output_dir=str(Path(args.output).parent),
    )


def main(argv: List[str]) -> int:
    """
    Program entrypoint for CLI use.

    Args:
        argv: Raw command-line argument list.

    Returns:
        Process exit code (0 success, non-zero failure).
    """
    parser = argparse.ArgumentParser(description="Build AArch64 loader artifacts (.exe/.dll/.sys) and package kernel image.")
    parser.add_argument("--name", help="Output artifact base name")
    parser.add_argument("--kind", choices=sorted(KIND_MAP.keys()), help="Artifact kind")
    parser.add_argument("--source", action="append", help="Source file path, repeatable")
    parser.add_argument("--include-dir", action="append", help="Include directory, repeatable")
    parser.add_argument("--define", action="append", help="Preprocessor define, repeatable")
    parser.add_argument("--cflag", action="append", help="Extra compiler flag, repeatable")
    parser.add_argument("--ldflag", action="append", help="Extra linker flag, repeatable")
    parser.add_argument("--entry-symbol", default="Init", help="Entry symbol name")
    parser.add_argument("--output-dir", default="build", help="Output directory for generated artifacts")
    parser.add_argument("--pack-elf", action="store_true", help="Pack an existing ELF image into a DLL0 artifact")
    parser.add_argument("--pack-kernel", action="store_true", help="Pack kernel ELF input into boot kernel-image output")
    parser.add_argument("--input", help="Input file path for kernel packing mode")
    parser.add_argument("--output", help="Output file path for kernel packing mode")
    parser.add_argument("--project-root", default=".", help="Root directory for relative source paths")
    args = parser.parse_args(argv)

    try:
        if args.pack_kernel:
            if not args.input or not args.output:
                raise ValueError("--pack-kernel mode requires --input and --output")
            pack_kernel_image(Path(args.input), Path(args.output))
            print(f"[ok] wrote {args.output}")
            return 0

        if args.pack_elf:
            cfg = build_pack_only_config(args)
            elf = Path(args.input).resolve()
            out_file = Path(args.output).resolve()
            sections, entry = collect_metadata(elf)

            if cfg.kind == "dll":
                inline_imports, inline_exports = extract_inline_metadata_from_elf(elf)
                if inline_imports:
                    cfg.imports = inline_imports
                if inline_exports:
                    cfg.exports = inline_exports
                else:
                    raise RuntimeError(f"{elf.name}: DLL exports must be declared with inline export metadata")
            elif cfg.kind == "sys":
                metadata = extract_kernel_module_metadata_from_elf(elf)
                if metadata:
                    cfg.name = str(metadata.get("name", cfg.name)).strip() or cfg.name
                    cfg.exports = synthesize_sys_exports_from_metadata(elf, metadata)
                    print(f"[meta] synthesized {len(cfg.exports)} SYS exports from embedded module metadata")
                else:
                    inline_imports, inline_exports = extract_inline_metadata_from_elf(elf)
                    if inline_imports:
                        cfg.imports = inline_imports
                    if inline_exports:
                        cfg.exports = inline_exports
                    else:
                        raise RuntimeError(f"{elf.name}: SYS exports must be declared with metadata")

            pack_image(cfg, elf, sections, entry, out_file, cfg.entry_symbol)
            print(f"[ok] wrote {out_file}")
            return 0

        cfg = build_config_from_args(args)
        project_root = Path(args.project_root).resolve()
        build_dir = (project_root / cfg.output_dir).resolve()
        build_dir.mkdir(parents=True, exist_ok=True)

        # Compile and link AArch64 objects into one ELF.
        objs = compile_sources(cfg, project_root, build_dir)
        apply_inline_metadata(cfg, objs)
        if cfg.kind == "dll" and not cfg.exports:
            raise RuntimeError(f"{cfg.name}: DLL exports must be declared with ROS_DLL_EXPORT metadata")
        if cfg.kind == "sys" and not cfg.exports:
            metadata = extract_kernel_module_metadata_from_elf(objs[0]) if len(objs) == 1 else None
            if metadata:
                cfg.name = str(metadata.get("name", cfg.name)).strip() or cfg.name
                cfg.exports = synthesize_sys_exports_from_metadata(objs[0], metadata)
                print(f"[meta] synthesized {len(cfg.exports)} SYS exports from embedded module metadata")
            else:
                raise RuntimeError(f"{cfg.name}: SYS exports must be declared with metadata")
        try:
            elf = link_elf(cfg, objs, build_dir)
        except RuntimeError:
            # Last-resort fallback for environments without any usable linker.
            if len(objs) != 1:
                raise
            elf = objs[0]

        # Parse ELF metadata and package loader image artifact.
        sections, entry = collect_metadata(elf)
        ext = "." + cfg.kind
        out_file = build_dir / f"{cfg.name}{ext}"
        pack_image(cfg, elf, sections, entry, out_file, cfg.entry_symbol)
        write_artifact_depfile(cfg, out_file, project_root, build_dir)

        print(f"[ok] wrote {out_file}")
        return 0
    except Exception as exc:  # noqa: BLE001
        print(f"[error] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
