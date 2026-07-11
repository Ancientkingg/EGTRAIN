#!/usr/bin/env python3
import os
import re
import sys

def clean_cpp_code(content: str) -> str:
    cleaned = []
    i = 0
    n = len(content)
    in_string = False
    string_char = None
    in_line_comment = False
    in_block_comment = False

    while i < n:
        char = content[i]
        next_char = content[i+1] if i + 1 < n else ""

        if in_line_comment:
            if char == '\n':
                in_line_comment = False
                cleaned.append('\n')
            else:
                cleaned.append(' ')
            i += 1
        elif in_block_comment:
            if char == '*' and next_char == '/':
                in_block_comment = False
                cleaned.append(' ')
                cleaned.append(' ')
                i += 2
            elif char == '\n':
                cleaned.append('\n')
                i += 1
            else:
                cleaned.append(' ')
                i += 1
        elif in_string:
            if char == '\\':
                if next_char == '\n':
                    cleaned.append(' ')
                    cleaned.append('\n')
                    i += 2
                else:
                    cleaned.append(' ')
                    cleaned.append(' ')
                    i += 2
            elif char == string_char:
                in_string = False
                cleaned.append(' ')
                i += 1
            elif char == '\n':
                cleaned.append('\n')
                i += 1
            else:
                cleaned.append(' ')
                i += 1
        else:
            if char == '/' and next_char == '/':
                in_line_comment = True
                cleaned.append(' ')
                cleaned.append(' ')
                i += 2
            elif char == '/' and next_char == '*':
                in_block_comment = True
                cleaned.append(' ')
                cleaned.append(' ')
                i += 2
            elif char == '"' or char == "'":
                in_string = True
                string_char = char
                cleaned.append(' ')
                i += 1
            else:
                cleaned.append(char)
                i += 1

    return "".join(cleaned)

def scan_codebase(repo_root: str) -> dict:
    results = {
        "Owning allocations": [],
        "Oversized arrays": [],
        "Non-owning pointers": []
    }

    target_dir = os.path.join(repo_root, "EGTRAIN", "QEGTRAIN")
    if not os.path.exists(target_dir):
        target_dir = repo_root

    keywords = {
        'return', 'delete', 'throw', 'typedef', 'using', 'friend', 'const',
        'static', 'extern', 'virtual', 'inline', 'class', 'struct', 'else', 'if', 'for', 'while'
    }

    # Regex for array declarations: Type Name[Size]; or Type Name[Size] =
    array_re = re.compile(r'\b([A-Za-z0-9_<>:]+)\s+([A-Za-z0-9_]+)\s*\[\s*([0-9]+)\s*\]\s*(?:;|=)')

    # Regex for pointer declarations: Type* Name or Type *Name or Type** Name
    pointer_re = re.compile(r'\b([A-Za-z0-9_<>:]+)\s*(\*+)\s*(?:const\s+)?([A-Za-z0-9_]+)\b')

    allowed_preceding = {';', '{', '}', '(', ',', ':', '*', '&'}

    for root, dirs, files in os.walk(target_dir):
        # Exclude io/third_party
        if "third_party" in root.split(os.sep):
            continue
        # Exclude build output or generated directories
        if any(x in root.split(os.sep) for x in ["build", "CMakeFiles", "cmake"]):
            continue

        for file in files:
            # Exclude generated files
            if file.startswith("moc_") or file.startswith("ui_") or file.startswith("qrc_"):
                continue

            ext = os.path.splitext(file)[1].lower()
            if ext not in [".h", ".hpp", ".cpp", ".cc", ".cxx", ".c"]:
                continue

            file_path = os.path.join(root, file)
            rel_path = os.path.relpath(file_path, repo_root)

            try:
                with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
            except Exception:
                continue

            cleaned_content = clean_cpp_code(content)

            lines = content.splitlines()
            cleaned_lines = cleaned_content.splitlines()

            for line_idx, (orig_line, clean_line) in enumerate(zip(lines, cleaned_lines), start=1):
                # 1. Check Owning Allocations
                has_owning = False
                if re.search(r'\b(?:new|malloc|calloc)\b', clean_line):
                    if not re.search(r'\b(?:delete|make_unique|make_shared)\b', clean_line):
                        has_owning = True

                if has_owning:
                    results["Owning allocations"].append((rel_path, line_idx, orig_line.strip()))
                    continue

                # 2. Check Oversized Arrays
                has_oversized = False
                for match in array_re.finditer(clean_line):
                    type_part, name_part, size_part = match.groups()
                    if type_part not in keywords and name_part not in keywords:
                        try:
                            size = int(size_part)
                            if size >= 100:
                                has_oversized = True
                                break
                        except ValueError:
                            pass
                if has_oversized:
                    results["Oversized arrays"].append((rel_path, line_idx, orig_line.strip()))
                    continue

                # 3. Check Non-owning Pointers
                has_pointer = False
                for match in pointer_re.finditer(clean_line):
                    type_part, stars, name_part = match.groups()
                    if type_part in keywords or name_part in keywords:
                        continue

                    start_pos = match.start()
                    pre_match_str = clean_line[:start_pos]

                    if '=' in pre_match_str:
                        continue

                    specifiers_re = re.compile(r'\b(?:const|static|extern|virtual|inline|friend|mutable)\b\s*')
                    pre_clean = specifiers_re.sub('', pre_match_str).strip()

                    if pre_clean:
                        last_char = pre_clean[-1]
                        if last_char not in allowed_preceding:
                            continue

                    has_pointer = True
                    break

                if has_pointer:
                    results["Non-owning pointers"].append((rel_path, line_idx, orig_line.strip()))

    for category in results:
        results[category].sort(key=lambda x: (x[0], x[1]))

    return results

def main():
    if len(sys.argv) > 1:
        repo_root = sys.argv[1]
    else:
        current_dir = os.path.dirname(os.path.abspath(__file__))
        repo_root = os.path.abspath(os.path.join(current_dir, "..", ".."))

    results = scan_codebase(repo_root)

    headings = ["Owning allocations", "Oversized arrays", "Non-owning pointers"]
    for i, heading in enumerate(headings):
        print(heading)
        for rel_path, line_no, content in results[heading]:
            print(f"{rel_path}:{line_no}: {content}")
        if i < len(headings) - 1:
            print()

if __name__ == "__main__":
    main()
