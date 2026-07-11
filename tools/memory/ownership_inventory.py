#!/usr/bin/env python3
import re
import sys
from pathlib import Path

HEADINGS = (
    "Owning allocations",
    "Qt-managed allocations",
    "Oversized arrays",
    "Non-owning pointers",
    "Unclassified raw pointers",
)
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
EXCLUDED_DIRS = {"CMakeFiles", "generated", "third_party", "vendor"}
CONFIRMED_OBSERVERS = {
    "EGTRAIN/QEGTRAIN/app/MainWindow.h": {
        "trainPaxInfoItem",
        "trainPaxItem",
        "paxIconInfoItem",
        "paxIconItem",
    },
    "EGTRAIN/QEGTRAIN/simulation/Rescheduling.h": {"TDS_arc"},
    "EGTRAIN/QEGTRAIN/simulation/SimulationWorker.h": {"s_active"},
    "EGTRAIN/QEGTRAIN/widgets/ConsoleWidget.h": {"m_oldCout", "m_oldCerr"},
}


def clean_cpp_code(content: str) -> str:
    cleaned = []
    i = 0
    state = "code"
    quote = ""
    while i < len(content):
        char = content[i]
        following = content[i + 1] if i + 1 < len(content) else ""
        if state == "line":
            cleaned.append("\n" if char == "\n" else " ")
            state = "code" if char == "\n" else state
            i += 1
        elif state == "block":
            if char == "*" and following == "/":
                cleaned.extend((" ", " "))
                state = "code"
                i += 2
            else:
                cleaned.append("\n" if char == "\n" else " ")
                i += 1
        elif state == "string":
            if char == "\\" and following:
                cleaned.extend((" ", "\n" if following == "\n" else " "))
                i += 2
            else:
                cleaned.append("\n" if char == "\n" else " ")
                state = "code" if char == quote else state
                i += 1
        else:
            raw = re.match(r'R"([^\s()\\]{0,16})\(', content[i:])
            if raw:
                terminator = f'){raw.group(1)}"'
                end = content.find(terminator, i + raw.end())
                end = len(content) if end < 0 else end + len(terminator)
                cleaned.extend("\n" if value == "\n" else " " for value in content[i:end])
                i = end
            elif char == "/" and following == "/":
                cleaned.extend((" ", " "))
                state = "line"
                i += 2
            elif char == "/" and following == "*":
                cleaned.extend((" ", " "))
                state = "block"
                i += 2
            elif char in {'"', "'"}:
                cleaned.append(" ")
                state = "string"
                quote = char
                i += 1
            else:
                cleaned.append(char)
                i += 1
    return "".join(cleaned)


def source_files(repo_root: Path):
    source_root = repo_root / "EGTRAIN/QEGTRAIN"
    for path in source_root.rglob("*"):
        relative_parts = path.relative_to(source_root).parts
        if any(
            part in EXCLUDED_DIRS
            or part.startswith("build")
            or part.startswith("cmake-build")
            for part in relative_parts[:-1]
        ):
            continue
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
            if not path.name.startswith(("moc_", "qrc_", "ui_")):
                yield path


def numeric_constants(files):
    constants = {}
    patterns = (
        re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)\s+(\d+)\b", re.MULTILINE),
        re.compile(
            r"\b(?:constexpr|const)\s+[A-Za-z_:<>][\w:<>]*\s+([A-Za-z_]\w*)\s*=\s*(\d+)\b"
        ),
    )
    for path in files:
        cleaned = clean_cpp_code(path.read_text(errors="replace"))
        for pattern in patterns:
            for name, value in pattern.findall(cleaned):
                constants[name] = int(value)
    return constants


def scan_codebase(repo_root):
    root = Path(repo_root).resolve()
    files = list(source_files(root))
    constants = numeric_constants(files)
    results = {heading: [] for heading in HEADINGS}
    allocation = re.compile(r"\b(?:new(?!\s*\()|malloc\s*\(|calloc\s*\(|realloc\s*\()")
    allocated_type = re.compile(r"\bnew(?!\s*\()\s+([A-Za-z_]\w*(?:::\w+)*)")
    array = re.compile(r"\b([A-Za-z_]\w*)\s*\[\s*(\d+|[A-Za-z_]\w*)\s*\]")
    declaration_prefix = re.compile(
        r"^\s*(?:(?:extern|static|const|constexpr|mutable|volatile|signed|unsigned|long|short)\s+)*"
        r"(?:struct\s+|class\s+)?[A-Za-z_]\w*(?:::\w+)*(?:\s*<[^;=()]+>)?\s*(?:[*&]\s*)?$"
    )
    pointer = re.compile(r"\b[A-Za-z_:<>][\w:<>]*\s*\*+\s*([A-Za-z_]\w*)")

    for path in files:
        relative = path.relative_to(root).as_posix()
        original_lines = path.read_text(errors="replace").splitlines()
        cleaned_lines = clean_cpp_code(path.read_text(errors="replace")).splitlines()
        observers = CONFIRMED_OBSERVERS.get(relative, set())
        for line_number, (original, cleaned) in enumerate(zip(original_lines, cleaned_lines), 1):
            evidence = (relative, line_number, original.strip())
            has_allocation = allocation.search(cleaned)
            if has_allocation:
                types = allocated_type.findall(cleaned)
                category = (
                    "Qt-managed allocations"
                    if types and all(name.startswith("Q") and not name.startswith("Ui::") for name in types)
                    else "Owning allocations"
                )
                results[category].append(evidence)
            array_matches = list(array.finditer(cleaned))
            is_declaration = array_matches and declaration_prefix.match(cleaned[: array_matches[0].start()])
            if is_declaration and any(
                    int(match.group(2)) >= 100
                    if match.group(2).isdigit()
                    else constants.get(match.group(2), 0) >= 100
                    for match in array_matches
            ):
                results["Oversized arrays"].append(evidence)
            pointer_names = pointer.findall(cleaned)
            confirmed = [name for name in pointer_names if name in observers]
            if confirmed:
                results["Non-owning pointers"].append(evidence)
            if pointer_names and not has_allocation and any(name not in observers for name in pointer_names):
                results["Unclassified raw pointers"].append(evidence)

    for values in results.values():
        values.sort(key=lambda item: (item[0], item[1]))
    return results


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
    results = scan_codebase(root)
    for index, heading in enumerate(HEADINGS):
        print(heading)
        for path, line_number, source in results[heading]:
            print(f"{path}:{line_number}: {source}")
        if index + 1 < len(HEADINGS):
            print()


if __name__ == "__main__":
    main()
