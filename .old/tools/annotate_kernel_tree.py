#!/usr/bin/env python3
"""Annotate kernel C sources with compact documentation comments.

This pass is intentionally conservative:
- keep existing comments intact,
- add short type comments where a type block is still bare,
- add function/procedure headers where definitions or prototypes are bare,
- add compact inline comments for simple declarations,
- add short block comments before standalone procedure calls when they begin a
  new logic step.
"""

from __future__ import annotations

import re
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
KERNEL_ROOT = ROOT / "kernel"

TYPE_RE = re.compile(
    r"^(?P<indent>\s*)(typedef\s+)?(?P<kind>struct|enum|union)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)?\s*\{\s*$"
)
FIELD_RE = re.compile(
    r"^(?P<indent>\s*)(?!return\b|if\b|for\b|while\b|switch\b|case\b|goto\b|typedef\b)(?:const\s+|static\s+|volatile\s+|unsigned\s+|signed\s+|struct\s+|enum\s+|union\s+|[A-Za-z_][A-Za-z0-9_\s\*]+\s+)+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*(?:=[^;]*)?;\s*$"
)
SIMPLE_CALL_RE = re.compile(r"^(?P<indent>\s*)(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\);\s*$")
FUNC_START_RE = re.compile(r"^(?P<indent>\s*)(?!if\b|for\b|while\b|switch\b|return\b)(?P<head>[^;]+?)\{\s*$")
PROTO_RE = re.compile(r"^(?P<indent>\s*)(?!typedef\b|#|if\b|for\b|while\b|switch\b)(?P<head>[^;{}]+\([^;{}]*\))\s*;\s*$")
NAME_FROM_HEAD_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\([^()]*\)\s*$")
COMMENT_START_RE = re.compile(r"^\s*/\*")
COMMENT_LINE_RE = re.compile(r"^\s*(?:/\*|\*|//)")
PREPROC_RE = re.compile(r"^\s*#")

VERB_HINTS = {
    "alloc": "Allocate",
    "append": "Append",
    "attach": "Attach",
    "begin": "Begin",
    "bind": "Bind",
    "block": "Block",
    "boot": "Bootstrap",
    "build": "Build",
    "call": "Call",
    "check": "Check",
    "cleanup": "Clean up",
    "close": "Close",
    "commit": "Commit",
    "copy": "Copy",
    "create": "Create",
    "destroy": "Destroy",
    "dispatch": "Dispatch",
    "dump": "Dump",
    "enable": "Enable",
    "exec": "Execute",
    "exit": "Handle",
    "find": "Find",
    "free": "Free",
    "get": "Get",
    "handle": "Handle",
    "init": "Initialize",
    "insert": "Insert",
    "kill": "Kill",
    "load": "Load",
    "lookup": "Look up",
    "map": "Map",
    "mark": "Mark",
    "mount": "Mount",
    "open": "Open",
    "pack": "Pack",
    "parse": "Parse",
    "prepare": "Prepare",
    "print": "Print",
    "probe": "Probe",
    "query": "Query",
    "read": "Read",
    "register": "Register",
    "release": "Release",
    "remove": "Remove",
    "reset": "Reset",
    "resolve": "Resolve",
    "resume": "Resume",
    "retain": "Retain",
    "return": "Return",
    "run": "Run",
    "schedule": "Schedule",
    "select": "Select",
    "send": "Send",
    "set": "Set",
    "setup": "Set up",
    "shutdown": "Shut down",
    "sleep": "Sleep",
    "start": "Start",
    "stop": "Stop",
    "suspend": "Suspend",
    "switch": "Switch",
    "sync": "Synchronize",
    "track": "Track",
    "translate": "Translate",
    "unblock": "Unblock",
    "unload": "Unload",
    "unmap": "Unmap",
    "unregister": "Unregister",
    "update": "Update",
    "validate": "Validate",
    "wake": "Wake",
    "write": "Write",
    "yield": "Yield",
    "zero": "Zero",
    "memzero": "Zero",
    "memset": "Reset",
    "kmalloc": "Allocate",
    "kfree": "Free",
    "calloc": "Allocate",
}

NOISE_TOKENS = {
    "sp",
    "ldr",
    "mm",
    "fs",
    "vfs",
    "hal",
    "gui",
    "cpu",
    "irq",
    "sys",
    "ros",
    "kernel",
}

DOMAIN_NOUNS = {
    "block",
    "boot",
    "cache",
    "context",
    "device",
    "disk",
    "driver",
    "file",
    "heap",
    "memory",
    "module",
    "page",
    "partition",
    "path",
    "process",
    "queue",
    "scheduler",
    "state",
    "task",
    "thread",
    "timer",
}

SINGLE_WORD_SUBJECTS = {
    "alloc": "memory through the active allocator",
    "free": "memory through the active allocator",
    "init": "scheduler state",
    "shutdown": "scheduler state",
    "yield": "the current execution slice",
    "sleep": "the current thread",
    "dispatch": "the next runnable thread",
    "schedule": "the next runnable task",
}

FIELD_HINTS = {
    "api": "/* Callback table supplied by the embedding kernel. */",
    "base": "/* Base address used by this object. */",
    "baseaddress": "/* Base address where this image or object is mapped. */",
    "begin": "/* Starting block or offset for this range. */",
    "buf": "/* Data buffer associated with this entry. */",
    "cache": "/* Cache storage associated with this object. */",
    "context": "/* Owning execution context for this operation. */",
    "count": "/* Number of items tracked in this scope. */",
    "dev": "/* Device identifier used by this mapping. */",
    "driver": "/* Driver callbacks used to access this device. */",
    "entry": "/* Entry point or table slot for this object. */",
    "fs_type": "/* Filesystem type recorded for this partition. */",
    "graphlock": "/* Lock that serializes graph and list mutations. */",
    "id": "/* Identifier for this object. */",
    "image": "/* Backing image information for this object. */",
    "index": "/* Loop index or slot selector for this scope. */",
    "kind": "/* Encoded kind for this record. */",
    "lba": "/* Logical block address tracked by this cache entry. */",
    "memory": "/* Memory region being allocated, cleared, or released. */",
    "module": "/* Module object being linked or inspected here. */",
    "name": "/* Short name stored for this object. */",
    "owner": "/* Owner reference for this object. */",
    "path": "/* Filesystem path associated with this object. */",
    "private": "/* Driver-private state carried with this object. */",
    "process": "/* Process object being created or traversed here. */",
    "result": "/* Result code returned to the caller. */",
    "size": "/* Size of this region or payload in bytes. */",
    "status": "/* Status code captured from the last operation. */",
    "task": "/* Task object being updated in this scope. */",
    "thread": "/* Thread object being created or traversed here. */",
}


def split_words(name: str) -> list[str]:
    name = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", name)
    tokens = re.split(r"[_\s]+", name)
    return [token.lower() for token in tokens if token]


def pretty_subject(name: str, drop_noise: bool = True) -> str:
    words = split_words(name)
    if drop_noise:
        while words and words[0] in NOISE_TOKENS:
            words.pop(0)
    if not words:
        words = split_words(name)
    return " ".join(words)


def article_for(words: list[str]) -> str:
    if not words:
        return "a"
    return "an" if words[0][:1] in {"a", "e", "i", "o", "u"} else "a"


def describe_function(name: str, path: Path) -> str:
    words = split_words(name)
    trimmed = [word for word in words if word not in NOISE_TOKENS] or words
    if len(trimmed) >= 2 and trimmed[0] in DOMAIN_NOUNS and trimmed[1] in VERB_HINTS:
        verb = VERB_HINTS[trimmed[1]]
        remainder = [trimmed[0], *trimmed[2:]]
    else:
        verb = VERB_HINTS.get(trimmed[0], "Handle") if trimmed else "Handle"
        remainder = trimmed[1:] if trimmed else []

    if not remainder and trimmed:
        subject = SINGLE_WORD_SUBJECTS.get(trimmed[0], pretty_subject(name))
    else:
        subject = " ".join(remainder) if remainder else "this subsystem operation"

    if subject.endswith(" name"):
        subject = subject[:-5] + " state name"
    if subject == "ref count":
        subject = "the current reference count"
    elif subject.startswith("current "):
        subject = "the " + subject
    elif subject in {"ticks", "tick count", "state", "context", "path", "info", "count", "memory through the active allocator", "scheduler state", "the current execution slice", "the current thread", "the next runnable thread", "the next runnable task"}:
        subject = "the " + subject
    elif subject.startswith("module") or subject.startswith("process") or subject.startswith("thread"):
        subject = subject
    else:
        art = article_for(subject.split())
        subject = f"{art} {subject}"

    area = path.parts[path.parts.index("kernel") + 1] if "kernel" in path.parts[:-1] else "kernel"
    if verb == "Handle":
        return f"Handle `{name}` work for the {area} subsystem."
    return f"{verb} {subject} for the {area} subsystem."


def describe_type(name: str, kind: str) -> str:
    subject = pretty_subject(name or kind, drop_noise=False)
    if subject.endswith(" struct") or subject.endswith(" enum") or subject.endswith(" union"):
        subject = subject.rsplit(" ", 1)[0]
    if kind == "enum":
        return f"/* Enum describing {subject}. */"
    if kind == "union":
        return f"/* Union overlay used for {subject}. */"
    return f"/* Struct describing {subject}. */"


def describe_field(name: str) -> str | None:
    words = split_words(name)
    if not words:
        return None
    normalized = "_".join(words)
    compact = "".join(words)
    if normalized in FIELD_HINTS:
        return FIELD_HINTS[normalized]
    if compact in FIELD_HINTS:
        return FIELD_HINTS[compact]
    if words[0] == "next":
        return f"/* Next {' '.join(words[1:]) or 'link'} in the local chain. */"
    if words[0] == "prev":
        return f"/* Previous {' '.join(words[1:]) or 'link'} in the local chain. */"
    if words[0] == "out":
        return f"/* Caller-owned output for {' '.join(words[1:]) or 'this result'}. */"
    if words[-1] == "count":
        return f"/* Number of {' '.join(words[:-1]) or 'items'} tracked here. */"
    if words[-1] == "size":
        return f"/* Size of {' '.join(words[:-1]) or 'this region'} in bytes. */"
    if words[-1] == "id":
        return f"/* Identifier for {' '.join(words[:-1]) or 'this object'}. */"
    if words[-1] == "head":
        return f"/* Head pointer for the {' '.join(words[:-1]) or 'list'}. */"
    if words[-1] == "tail":
        return f"/* Tail pointer for the {' '.join(words[:-1]) or 'queue'}. */"
    if words[0] == "is":
        return f"/* Flag telling whether {' '.join(words[1:]) or 'this state'} is true. */"
    if words[0] == "has":
        return f"/* Flag telling whether {' '.join(words[1:]) or 'this feature'} is available. */"
    if words[0] in {"current", "active"}:
        return f"/* Current {' '.join(words[1:]) or 'value'} in use. */"
    if words[0] in {"base", "preferred"}:
        return f"/* Base {' '.join(words[1:]) or 'value'} used by this object. */"
    if words[0] in {"owner", "parent"}:
        return f"/* Owning {' '.join(words[1:]) or 'object'} reference. */"
    return f"/* Holds {' '.join(words)} for this scope. */"


def describe_call(name: str) -> str:
    words = split_words(name)
    trimmed = [word for word in words if word not in NOISE_TOKENS] or words
    if len(trimmed) >= 2 and trimmed[0] in DOMAIN_NOUNS and trimmed[1] in VERB_HINTS:
        verb = VERB_HINTS[trimmed[1]]
        remainder = " ".join([trimmed[0], *trimmed[2:]]) or "the related object"
    else:
        verb = VERB_HINTS.get(trimmed[0], "Handle") if trimmed else "Handle"
        remainder = " ".join(trimmed[1:]) if len(trimmed) > 1 else pretty_subject(name)
    if verb == "Handle":
        return f"/* Handle {remainder}. */"
    return f"/* {verb} {remainder}. */"


def previous_meaningful(lines: list[str], index: int) -> int | None:
    cursor = index - 1
    while cursor >= 0:
        if lines[cursor].strip():
            return cursor
        cursor -= 1
    return None


def has_comment_before(lines: list[str], index: int) -> bool:
    prev = previous_meaningful(lines, index)
    if prev is None:
        return False
    return bool(COMMENT_LINE_RE.match(lines[prev]))


def ends_comment(line: str) -> bool:
    return "/*" in line or "//" in line


def append_inline_comment(line: str, comment: str) -> str:
    if ends_comment(line):
        return line
    stripped = line.rstrip("\n")
    return f"{stripped} {comment}\n"


def collect_toplevel_signature(lines: list[str], start: int) -> tuple[int, str, str] | None:
    cursor = start
    paren_depth = 0
    saw_paren = False
    chunks: list[str] = []

    while cursor < len(lines):
        line = lines[cursor]
        stripped = line.strip()
        if not stripped:
            return None
        if COMMENT_LINE_RE.match(line) or PREPROC_RE.match(line):
            return None

        chunks.append(stripped)
        paren_depth += line.count("(") - line.count(")")
        saw_paren = saw_paren or ("(" in line)

        if paren_depth == 0 and stripped.endswith(";"):
            if saw_paren:
                return cursor, " ".join(chunks), "prototype"
            return None
        if paren_depth == 0 and stripped.endswith("{"):
            if saw_paren:
                return cursor, " ".join(chunks), "function"
            return None
        cursor += 1

    return None


def extract_name(signature: str) -> str | None:
    signature = signature.replace("{", " ").replace(";", " ").strip()
    if "(*" in signature:
        return None
    match = NAME_FROM_HEAD_RE.search(signature)
    if not match:
        return None
    name = match.group(1)
    if name in {"if", "for", "while", "switch", "return", "sizeof"}:
        return None
    return name


def annotate_file(path: Path) -> bool:
    original = path.read_text(encoding="utf-8")
    lines = original.splitlines(keepends=True)
    output: list[str] = []
    changed = False
    i = 0
    inside_type = False
    type_brace_depth = 0
    brace_depth = 0

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        type_match = TYPE_RE.match(line)
        if brace_depth == 0 and type_match:
            if not has_comment_before(output, len(output)):
                output.append(type_match.group("indent") + describe_type(type_match.group("name") or type_match.group("kind"), type_match.group("kind")) + "\n")
                changed = True
            inside_type = True
            type_brace_depth = 1
            output.append(line)
            i += 1
            continue

        if inside_type:
            type_brace_depth += line.count("{") - line.count("}")
            field_match = FIELD_RE.match(line)
            if field_match and type_brace_depth >= 1 and not ends_comment(line) and not PREPROC_RE.match(line):
                comment = describe_field(field_match.group("name"))
                if comment:
                    output.append(append_inline_comment(line, comment))
                    changed = True
                else:
                    output.append(line)
            else:
                output.append(line)
            if type_brace_depth <= 0:
                inside_type = False
            i += 1
            continue

        if brace_depth == 0 and stripped and not COMMENT_LINE_RE.match(line) and not PREPROC_RE.match(line):
            collected = collect_toplevel_signature(lines, i)
            if collected is not None:
                end_index, signature, kind = collected
                name = extract_name(signature)
                if name:
                    first_line = lines[i]
                    indent = re.match(r"^\s*", first_line).group(0)
                    if kind == "prototype":
                        if path.suffix == ".h" and not has_comment_before(output, len(output)):
                            output.append(indent + f"/* {describe_function(name, path)} */\n")
                            changed = True
                    else:
                        if not has_comment_before(output, len(output)):
                            output.extend([
                                indent + "/*\n",
                                indent + f" * {name}\n",
                                indent + " *\n",
                                indent + f" * {describe_function(name, path)}\n",
                                indent + " */\n",
                            ])
                            changed = True
                for cursor in range(i, end_index + 1):
                    output.append(lines[cursor])
                    brace_depth += lines[cursor].count("{") - lines[cursor].count("}")
                i = end_index + 1
                continue

        field_match = FIELD_RE.match(line)
        if brace_depth > 0 and field_match and not ends_comment(line) and not PREPROC_RE.match(line):
            prev = previous_meaningful(output, len(output))
            if prev is not None:
                prev_line = output[prev].strip()
            else:
                prev_line = ""
            if prev_line in {"{", "}"} or prev_line.endswith("{") or prev_line == "":
                comment = describe_field(field_match.group("name"))
                if comment:
                    output.append(append_inline_comment(line, comment))
                    changed = True
                    i += 1
                    continue

        call_match = SIMPLE_CALL_RE.match(line)
        if brace_depth > 0 and call_match and not has_comment_before(output, len(output)):
            prev = previous_meaningful(output, len(output))
            prev_line = output[prev].strip() if prev is not None else ""
            if prev is None or prev_line.endswith("{") or prev_line == "":
                output.append(call_match.group("indent") + describe_call(call_match.group("name")) + "\n")
                changed = True

        output.append(line)
        brace_depth += line.count("{") - line.count("}")
        i += 1

    rewritten = "".join(output)
    if rewritten != original:
        path.write_text(rewritten, encoding="utf-8")
        return True
    return changed


def iter_targets(arguments: list[str]) -> list[Path]:
    if not arguments:
        return [path for path in sorted(KERNEL_ROOT.rglob("*")) if path.suffix in {".c", ".h"}]

    targets: list[Path] = []
    for argument in arguments:
        candidate = (ROOT / argument).resolve() if not argument.startswith("/") else Path(argument).resolve()
        if candidate.is_dir():
            targets.extend(path for path in sorted(candidate.rglob("*")) if path.suffix in {".c", ".h"})
        elif candidate.suffix in {".c", ".h"} and candidate.exists():
            targets.append(candidate)
    return sorted(dict.fromkeys(targets))


def main(argv: list[str]) -> int:
    changed_files: list[Path] = []
    for path in iter_targets(argv[1:]):
        if annotate_file(path):
            changed_files.append(path)

    for path in changed_files:
        print(path.relative_to(ROOT))
    print(f"changed={len(changed_files)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))