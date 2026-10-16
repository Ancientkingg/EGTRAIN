#!/usr/bin/env python3
"""Static check of the scene codec.

Each key of the scene files is written in one place, SceneWriter.cpp, and read in one place, the loader in
SceneModel.cpp with SceneFieldReader.cpp. SceneFields.h holds a key table that both sides use. The test scans the
C++ files in EGTRAIN/QEGTRAIN/scene that handle JSON and collects the keys that the codec files hold. It fails when

- a key of the codec appears in another file, as a subscript, a pair in a JSON initializer, a call of contains, value or
  at, or a call of a SceneFieldReader field reader, unless EXCEPTIONS names the file and the key with the reason;
- SceneWriter.cpp reads a key, or the loader writes one;
- an exception no longer applies;
- a file in the scene folder includes a header of the simulation folder.

A string that is not in one of those positions is not a key: diagnostic paths and item kinds pass. Comments are not
scanned. To allow a new use, add the key to EXCEPTIONS with the reason.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCENE_ROOT = ROOT / "EGTRAIN/QEGTRAIN/scene"

WRITERS = {"SceneWriter.cpp", "SceneFields.h"}
READERS = {"SceneModel.cpp", "SceneFieldReader.cpp", "SceneFieldReader.h", "SceneFields.h"}

# (file, key): why the file may hold the key although it is not a codec file.
EXCEPTIONS = {
    ("SceneBundle.cpp", "schema_version"): "the bundle probe reads the version of scene.json before the loader decides to extract",
    ("SceneBundle.cpp", "saved_with_app_version"): "the bundle probe reads the version of scene.json before the loader decides to extract",
    ("SceneCompatibility.cpp", "schema_version"): "the compatibility probe reads the version before the loader reads the scene",
    ("SceneCompatibility.cpp", "saved_with_app_version"): "the compatibility probe reads the version before the loader reads the scene",
    ("SceneExporter.cpp", "signalling_areas"): "the area sidecar of a legacy folder holds the list under this key; encodeSignallingArea writes its items",
    ("SceneImporter.cpp", "signalling_areas"): "the importer reads the same sidecar; decodeSignallingArea reads its items",
}

# A name in a string literal that can be a field of a JSON object.
NAME = r"[A-Za-z_][A-Za-z0-9_]*"
WRITE_SITES = (
    re.compile(r'\["(' + NAME + r')"\]\s*=(?!=)'),
    re.compile(r'\{\s*"(' + NAME + r')"\s*,'),
)
READ_SITES = (
    re.compile(r'\.(?:contains|value|at)\(\s*"(' + NAME + r')"'),
    re.compile(r'\b(?:stringField|numberField|integerField|boolField|arraySection)\s*\((?:[^;"]|"[^"\\]*")*?,\s*"(' + NAME + r')"'),
)
SUBSCRIPT = re.compile(r'\["(' + NAME + r')"\]')
INCLUDE_SIMULATION = re.compile(r'^\s*#\s*include\s*[<"]simulation/', re.MULTILINE)
USES_JSON = re.compile(r"\bnlohmann\b|\bjson\b")

SUFFIXES = {".cpp", ".h"}


def strip_comments(text):
    """Return (code, blank): the text without comments, and the same with the inside of each string literal blanked.

    Both keep the line breaks, so a position in one is the line in the other.
    """
    code = []
    blank = []
    i = 0
    n = len(text)

    def both(piece, blanked=None):
        code.append(piece)
        blank.append(piece if blanked is None else blanked)

    while i < n:
        c = text[i]
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            both(" " * (end - i))
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            chunk = text[i:end]
            both("".join("\n" if ch == "\n" else " " for ch in chunk))
            i = end
        elif c == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            paren = text.find("(", i)
            delimiter = text[i + 2 : paren]
            end = text.find(")" + delimiter + '"', paren)
            end = n if end < 0 else end + len(delimiter) + 2
            chunk = text[i:end]
            both(chunk, "".join("\n" if ch == "\n" else " " for ch in chunk))
            i = end
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            chunk = text[i : j + 1]
            both(chunk, c + "".join("\n" if ch == "\n" else " " for ch in chunk[1:-1]) + c)
            i = j + 1
        else:
            both(c)
            i += 1
    return "".join(code), "".join(blank)


def key_sites(text):
    """Return (writes, reads, subscripts): the sets of keys that sit in a position of that kind."""
    code, blank = strip_comments(text)
    if not USES_JSON.search(blank):
        return set(), set(), set()
    writes = {m.group(1) for pattern in WRITE_SITES for m in pattern.finditer(code)}
    reads = {m.group(1) for pattern in READ_SITES for m in pattern.finditer(code)}
    subscripts = {m.group(1) for m in SUBSCRIPT.finditer(code)}
    return writes, reads, subscripts


def scene_files(root):
    return {
        path.name: path.read_text(encoding="utf-8", errors="replace")
        for path in sorted(root.iterdir())
        if path.suffix in SUFFIXES
    }


def check_codec(files, exceptions=EXCEPTIONS):
    """Return the problems of the key census of files, a dict of file name to text."""
    sites = {name: key_sites(text) for name, text in files.items()}
    schema_keys = set()
    for name in WRITERS | READERS:
        if name in sites:
            schema_keys |= sites[name][0] | sites[name][1] | sites[name][2]
    problems = []
    used = set()
    for name, (writes, reads, subscripts) in sorted(sites.items()):
        if name in WRITERS | READERS:
            if name not in READERS:
                problems += [f"{name}: reads the key {key!r}, which only the loader reads" for key in sorted(reads)]
            if name not in WRITERS:
                problems += [f"{name}: writes the key {key!r}, which only SceneWriter.cpp writes" for key in sorted(writes)]
            continue
        for key in sorted((writes | reads | subscripts) & schema_keys):
            if (name, key) in exceptions:
                used.add((name, key))
            else:
                problems.append(f"{name}: holds the key {key!r} of the scene files, which only the codec files may hold")
    problems += [f"{name}: the exception for {key!r} no longer applies" for (name, key) in sorted(set(exceptions) - used)]
    return problems


def check_includes(files):
    """Return the problems of the includes of files: no header of the simulation folder."""
    return [f"{name}: includes a header of the simulation folder" for name, text in sorted(files.items()) if INCLUDE_SIMULATION.search(strip_comments(text)[0])]


def check_patterns():
    """Return the failed self-checks, run on fixture text before the scan."""
    failures = []

    def expect(what, got, expected):
        if got != expected:
            failures.append(f"self-check: {what}: expected {expected!r}, got {got!r}")

    codec = {
        "SceneWriter.cpp": '#include <nlohmann/json.hpp>\nvoid f(json& v) { v["id"] = 1; v["name"] = 2; json w = {{"start", 1}}; }\n',
        "SceneModel.cpp": '#include <nlohmann/json.hpp>\nvoid g(json& v) { reader.stringField(v["x"], "id", "p", out); reader.stringField(v, "name", "p", out); }\n',
    }

    def run(extra, exceptions=None):
        return check_codec({**codec, **extra}, exceptions or {})

    expect("the codec files alone pass", run({}), [])
    expect("a subscript is found", len(run({"A.cpp": '#include <nlohmann/json.hpp>\nauto x = v["id"];'})), 1)
    expect("an assignment is found", len(run({"A.cpp": '#include <nlohmann/json.hpp>\nv["name"] = 1;'})), 1)
    expect("a pair is found", len(run({"A.cpp": '#include <nlohmann/json.hpp>\njson v = {{"name", 1}};'})), 1)
    expect("contains is found", len(run({"A.cpp": '#include <nlohmann/json.hpp>\nif (v.contains("id")) {}'})), 1)
    expect("a field reader is found", len(run({"A.cpp": '#include <nlohmann/json.hpp>\nreader.numberField(v["a"], "id", "p", x);'})), 1)
    expect("a key in a comment is not found", run({"A.cpp": '#include <nlohmann/json.hpp>\n// v["id"] = 1;\n/* v["name"] */'}), [])
    expect("a key that is not in the codec is not found", run({"A.cpp": '#include <nlohmann/json.hpp>\nv["other"] = 1;'}), [])
    expect("a file without JSON is not scanned", run({"A.cpp": 'void f() { add({"id", 1}); }'}), [])
    expect("a string that is not at a key site is not found", run({"A.cpp": '#include <nlohmann/json.hpp>\nstd::string p = "id";'}), [])
    expect("an exception is used", run({"A.cpp": '#include <nlohmann/json.hpp>\nv["id"] = 1;'}, {("A.cpp", "id"): "why"}), [])
    expect("an exception that does not apply is found", len(run({}, {("A.cpp", "id"): "why"})), 1)
    expect("the loader writing a key is found", len(check_codec({**codec, "SceneModel.cpp": codec["SceneModel.cpp"] + 'void h(json& v) { v["id"] = 1; }'}, {})), 1)
    expect("the writer reading a key is found", len(check_codec({**codec, "SceneWriter.cpp": codec["SceneWriter.cpp"] + 'bool h(json& v) { return v.contains("id"); }'}, {})), 1)
    expect("an include of the simulation folder is found", len(check_includes({"A.cpp": '#include "simulation/RuntimeLimits.h"\n'})), 1)
    expect("an include of the scene folder is not found", check_includes({"A.cpp": '#include "scene/RuntimeLimits.h"\n// #include "simulation/X.h"\n'}), [])
    code, blank = strip_comments('a // b "c"\n"d // e" \'"\'')
    expect("comments are removed and strings are kept", code, 'a ' + ' ' * 8 + '\n"d // e" \'"\'')
    expect("the inside of a string is blanked", blank, 'a ' + ' ' * 8 + '\n"      " \' \'')
    expect("a raw string is kept", strip_comments('x R"k(// "id")k" y')[0], 'x R"k(// "id")k" y')
    return failures


def main():
    problems = check_patterns()
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    files = scene_files(SCENE_ROOT)
    for name in sorted(WRITERS | READERS):
        if name not in files:
            problems.append(f"{name} was not scanned")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    writes, reads, _ = key_sites(files["SceneWriter.cpp"])
    loader_reads = key_sites(files["SceneModel.cpp"])[1]
    if len(writes) < 50 or len(loader_reads) < 50:
        problems.append(f"the census found {len(writes)} written and {len(loader_reads)} read keys; the patterns no longer match the code")
    problems += check_codec(files)
    problems += check_includes(files)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    print(f"{len(writes)} keys written and {len(loader_reads)} keys read in the codec; {len(files)} scene files scanned")
    return 0


if __name__ == "__main__":
    sys.exit(main())
