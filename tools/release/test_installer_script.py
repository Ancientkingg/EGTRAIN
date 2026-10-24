#!/usr/bin/env python3
"""Static checks of installer/egtrain.iss, the Inno Setup script of the Windows installer.

The test reads the script, installer/installed-by-setup.ini, docs/development/windows-installer.md, the icon file, the licence file, the
Windows package job in .github/workflows/package.yml and the asset names in update/ReleaseInfo.cpp, and holds them to the rules of check_script. It does not compile the script
and does not run an installer. A green run therefore shows nothing about Inno Setup accepting the file, or
about an install, an upgrade or an uninstall on Windows.
"""
import posixpath
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INSTALLER = "installer"
SCRIPT = INSTALLER + "/egtrain.iss"
README = "docs/development/windows-installer.md"
MARKER = "installed-by-setup.ini"
RELEASE_INFO = "EGTRAIN/QEGTRAIN/update/ReleaseInfo.cpp"
LICENSE = "LICENSE"
PACKAGE_WORKFLOW = ".github/workflows/package.yml"

APP_ID = "{{16796959-230D-4A37-879B-D3963171F664}"
PROGRAM = "{app}\\QEGTRAIN.exe"
PAYLOAD_SOURCE = "{#Payload}\\*"
SCENES = "{app}\\Scenes"
# The files of a Qt 5 installation (1.0.3 and older) that no file of a Qt 6 package replaces.
QT5_FILES = (
    "{app}\\Qt5*.dll",
    "{app}\\bearer",
    "{app}\\styles\\qwindowsvistastyle.dll",
    "{app}\\imageformats\\qicns.dll",
    "{app}\\imageformats\\qtga.dll",
    "{app}\\imageformats\\qtiff.dll",
    "{app}\\imageformats\\qwbmp.dll",
    "{app}\\imageformats\\qwebp.dll",
)
CLASSES = "Software\\Classes\\"
PROG_ID = "EGTRAIN.Scene"
# The entries of the file type by Subkey: the ValueData (None for free text) and the Flags, which are compared as a set.
ASSOCIATION = {
    CLASSES + ".egscene": (PROG_ID, {"uninsdeletevalue", "uninsdeletekeyifempty"}),
    CLASSES + PROG_ID: (None, {"uninsdeletekey"}),
    CLASSES + PROG_ID + "\\DefaultIcon": (PROGRAM + ",0", {"uninsdeletekey"}),
    CLASSES + PROG_ID + "\\shell\\open\\command": ('"' + PROGRAM + '" "%1"', {"uninsdeletekey"}),
}
SECTIONS = ("setup", "tasks", "files", "installdelete", "icons", "registry")
CONSTANTS = ("app", "userpf", "userprograms", "userdesktop")
MACROS = ("Version", "Payload")
SETUP_PINS = (
    ("AppId", APP_ID),
    ("AppName", "EGTRAIN"),
    ("AppVersion", "{#Version}"),
    ("UninstallDisplayName", "EGTRAIN"),
    ("DefaultDirName", "{userpf}\\EGTRAIN"),
    ("PrivilegesRequired", "lowest"),
    ("ArchitecturesAllowed", "x64compatible"),
    ("ArchitecturesInstallIn64BitMode", "x64compatible"),
    ("CloseApplications", "yes"),
    ("ChangesAssociations", "yes"),
    ("UninstallDisplayIcon", PROGRAM),
    ("OutputBaseFilename", "EGTRAIN-Setup-{#Version}-x64"),
    ("LicenseFile", "..\\" + LICENSE),
)
SETUP_FORBIDDEN = ("PrivilegesRequiredOverridesAllowed", "UninstallFilesDir", "CreateUninstallRegKey", "Uninstallable")

SECTION_HEADER = re.compile(r"\[([^\]]*)\]")
DIRECTIVE = re.compile(r"#(ifndef (Version|Payload)|error .+|endif)")
MACRO = re.compile(r"\{#([^{}]*)\}")
CONSTANT = re.compile(r"\{([^{}]*)\}")
APP_ID_FORM = re.compile(r"\{\{[0-9A-Fa-f]{8}(-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}\}")
ICON_PREFIX = re.compile(r"^(\s*(?:SetupIconFile|LicenseFile)\s*=\s*)\.\.\\", re.IGNORECASE | re.MULTILINE)
WINDOWS_JOB = re.compile(r"^  package-windows:\n(.*?)(?=^  \S)", re.DOTALL | re.MULTILINE)
LICENSE_COPY = re.compile(r'^\s*Copy-Item\s+LICENSE\s+"\$dist/LICENSE"\s*$', re.MULTILINE)
LICENSE_CHECK = re.compile(r'^\s*if \(-not \(Test-Path "\$dist/LICENSE"\)\) \{ throw "[^"]*" \}\s*$', re.MULTILINE)
PLAIN_NAME = re.compile(r"[A-Za-z0-9_.-]+")
ASSET_PATTERN = re.compile(r'\{\s*"windows-x64-installer"\s*,\s*"([^"]+)"\s*\}')


def read_repository_file(path):
    """The bytes of the file at path (below ROOT, forward slashes), or None when there is no such file."""
    target = ROOT.joinpath(*path.split("/"))
    return target.read_bytes() if target.is_file() else None


def script_text(data):
    """Decodes the bytes of a file as ASCII. A byte outside ASCII becomes U+FFFD, which check_script reports."""
    return data.decode("ascii", errors="replace")


def split_parameters(line):
    """Splits an entry line at the semicolons outside double quotes. Also tells whether a quote is left open."""
    parts, current, quoted = [], [], False
    for char in line:
        if char == '"':
            quoted = not quoted
        if char == ";" and not quoted:
            parts.append("".join(current))
            current = []
        else:
            current.append(char)
    parts.append("".join(current))
    return parts, quoted


def parse_entry(line, number, problems):
    """The "Key: value" parameters of an entry line by lower-case name, without the quotes around a value.

    A doubled quote inside a quoted value is one quote."""
    parts, unmatched = split_parameters(line)
    if unmatched:
        problems.append(f"syntax: line {number} has a double quote that is not closed")
    params = {}
    for part in parts:
        if not part.strip():
            continue
        name, colon, value = part.partition(":")
        name = name.strip().lower()
        value = value.strip()
        if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
            value = value[1:-1].replace('""', '"')
        if not colon or not name:
            problems.append(f"syntax: line {number} has the parameter {part.strip()!r} without Name: value")
        elif name in params:
            problems.append(f"syntax: line {number} repeats the parameter {name}")
        else:
            params[name] = value
    return params


def flag_set(params):
    return set(params.get("flags", "").lower().split())


def split_text(text, problems):
    """The directive lines and the lines of each section as (number, line). Comments and blank lines are dropped."""
    directives, sections, current = [], {}, None
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("#"):
            directives.append((number, line))
            continue
        header = SECTION_HEADER.fullmatch(line)
        if header:
            current = header.group(1).strip().lower()
            sections.setdefault(current, [])
            if current not in SECTIONS:
                problems.append(f"section: [{header.group(1)}] is not allowed")
        elif current is None:
            problems.append(f"syntax: line {number} is outside any section")
        else:
            sections[current].append((number, line))
    return directives, sections


def check_text(text, problems):
    """The rules that read the whole text, comments included."""
    for char in text:
        if ord(char) > 127:
            problems.append(f"ascii: the script holds U+{ord(char):04X}, which is not ASCII")
            break
    lowered = text.lower()
    if "hklm" in lowered or "hkey_local_machine" in lowered:
        problems.append("hklm: nothing may name HKLM or HKEY_LOCAL_MACHINE")
    if ".." in ICON_PREFIX.sub(r"\1", text):
        problems.append("parent-path: no path may contain .. (only SetupIconFile and LicenseFile may start with ..\\)")
    if len(APP_ID_FORM.findall(text)) != 1 or text.count(APP_ID[2:-1]) != 1:
        problems.append("app-id: the AppId must appear once, as {{ GUID }")


def check_directives(directives, problems):
    for number, line in directives:
        if not DIRECTIVE.fullmatch(line):
            problems.append(f"directive: line {number} is {line!r}; only #ifndef Version, #ifndef Payload, #error and #endif are allowed")
    lines = [line for _, line in directives]
    for name in MACROS:
        guard = f"#ifndef {name}"
        if guard not in lines:
            problems.append(f"macro: there is no {guard} guard")
            continue
        after = lines[lines.index(guard) + 1:][:2]
        if len(after) < 2 or not after[0].startswith("#error ") or f"/D{name}" not in after[0] or after[1] != "#endif":
            problems.append(f"macro: {guard} must be followed by an #error that names /D{name} and by #endif")


def check_braces(sections, problems):
    """The macros and the constants of every line except the AppId line."""
    for lines in sections.values():
        for number, line in lines:
            if line.partition("=")[0].strip().lower() == "appid":
                continue
            for macro in MACRO.findall(line):
                if macro.strip() not in MACROS:
                    problems.append(f"macro: line {number} uses {{#{macro}}}; only {{#Version}} and {{#Payload}} are allowed")
            rest = MACRO.sub("", line)
            for constant in CONSTANT.findall(rest):
                if constant not in CONSTANTS:
                    problems.append(f"constant: line {number} uses {{{constant}}}; only {', '.join(CONSTANTS)} are allowed")
            if re.search(r"[{}]", CONSTANT.sub("", rest)):
                problems.append(f"constant: line {number} has a brace that is not part of a constant")


def check_setup(lines, read, problems):
    """Checks the [Setup] directives and returns them by lower-case name."""
    values = {}
    for number, line in lines:
        key, equals, value = line.partition("=")
        key = key.strip().lower()
        if not equals or not key:
            problems.append(f"syntax: line {number} of [Setup] is not Key=value")
        elif key in values:
            problems.append(f"setup-duplicate: {key} is set twice")
        else:
            values[key] = value.strip()
    for key, expected in SETUP_PINS:
        found = values.get(key.lower())
        if found != expected:
            problems.append(f"setup-pin: {key} must be {expected}" + (" (it is missing)" if found is None else f", not {found}"))
    for key in SETUP_FORBIDDEN:
        if key.lower() in values:
            problems.append(f"setup-forbidden: {key} must not be set")
    icon = values.get("setupiconfile")
    if icon is not None:
        path = posixpath.normpath(INSTALLER + "/" + icon.replace("\\", "/"))
        if path.startswith("..") or read(path) is None:
            problems.append(f"icon-file: SetupIconFile {icon} is not a file")
    return values


def check_license(values, read, problems):
    """The licence page shows the GPL-3.0 text of the repository, and the Windows package job puts that file into the package.

    The package is installed as it is and also becomes the portable archive, so a LICENSE in the package reaches both."""
    data = read(LICENSE)
    text = data.decode("utf-8", errors="replace") if data is not None else ""
    if "GNU GENERAL PUBLIC LICENSE" not in text or "Version 3, 29 June 2007" not in text:
        problems.append(f"license: {LICENSE} is missing or is not the GPL-3.0 text")
    page = values.get("licensefile")
    if page is not None and posixpath.normpath(INSTALLER + "/" + page.replace("\\", "/")) != LICENSE:
        problems.append(f"license: LicenseFile must name the {LICENSE} of the repository, not {page}")
    workflow = read(PACKAGE_WORKFLOW)
    # A Windows checkout can have CRLF line ends, and the job pattern matches LF only.
    workflow_text = workflow.decode("utf-8", errors="replace").replace("\r\n", "\n") if workflow is not None else None
    job = WINDOWS_JOB.search(workflow_text + "\n  end:\n") if workflow_text is not None else None
    if job is None:
        problems.append(f"license: the package-windows job was not found in {PACKAGE_WORKFLOW}")
    else:
        if not LICENSE_COPY.search(job.group(1)):
            problems.append(f"license: the package-windows job must copy {LICENSE} into the package")
        if not LICENSE_CHECK.search(job.group(1)):
            problems.append(f"license: the package-windows job must check that the package holds {LICENSE}")


def check_output_name(values, read, problems):
    """The installer name must be one that the updater accepts for the key windows-x64-installer."""
    name = values.get("outputbasefilename")
    if name is None:
        return
    source = read(RELEASE_INFO)
    patterns = ASSET_PATTERN.findall(source.decode("utf-8", errors="replace")) if source is not None else []
    if not patterns:
        problems.append(f"asset-name: the windows-x64-installer entry of kAssetPatterns was not found in {RELEASE_INFO}")
        return
    for version in ("1.2.3", "10.20.30"):
        accepted = [pattern.replace("%1", version) for pattern in patterns]
        produced = name.replace("{#Version}", version) + ".exe"
        if produced not in accepted:
            problems.append(f"asset-name: {produced} is not one of the names {accepted} that the updater accepts")


def check_tasks(entries, problems):
    if [params.get("name") for _, params in entries] != ["desktopicon"]:
        problems.append("tasks: the only task is desktopicon")
    for number, params in entries:
        if "unchecked" not in flag_set(params):
            problems.append(f"tasks: line {number}: a task must be unchecked by default")


def check_files(entries, read, problems):
    payload = [params for _, params in entries if params.get("source") == PAYLOAD_SOURCE]
    if len(payload) != 1:
        problems.append(f"files: exactly one entry must install {PAYLOAD_SOURCE}")
    elif not {"ignoreversion", "recursesubdirs"} <= flag_set(payload[0]):
        problems.append("files: the payload entry needs the flags ignoreversion and recursesubdirs")
    markers = [params for _, params in entries if params.get("source", "").lower() == MARKER]
    if len(markers) != 1 or markers[0].get("destdir") != "{app}":
        problems.append(f"files: exactly one entry must install {MARKER} into {{app}}")
    data = read(INSTALLER + "/" + MARKER)
    if data is None or not data.strip():
        problems.append(f"files: {INSTALLER}/{MARKER} is missing or empty")
    for number, params in entries:
        destdir = params.get("destdir", "")
        if destdir != "{app}" and not destdir.startswith("{app}\\"):
            problems.append(f"files: line {number}: DestDir must be {{app}} or a folder below it")
        flags = params.get("flags", "").lower()
        if "external" in flags or "download" in flags:
            problems.append(f"files: line {number}: a file must not be external or downloaded")
        source = params.get("source", "")
        if source != PAYLOAD_SOURCE and (not PLAIN_NAME.fullmatch(source) or read(INSTALLER + "/" + source) is None):
            problems.append(f"files: line {number}: Source {source} must be {PAYLOAD_SOURCE} or a file in {INSTALLER}/")


def check_install_delete(entries, problems):
    for number, params in entries:
        if params.get("name") not in (SCENES, *QT5_FILES):
            problems.append(f"install-delete: line {number}: only {SCENES} and the Qt 5 files may be deleted, not {params.get('name')}")
    names = [params.get("name") for _, params in entries]
    if SCENES not in names:
        problems.append(f"install-delete: {SCENES} must be deleted before an install")
    for name in sorted(set(QT5_FILES) - set(names)):
        problems.append(f"install-delete: {name} of a Qt 5 installation must be deleted")


def check_icons(entries, problems):
    programs, desktop = "{userprograms}\\EGTRAIN", "{userdesktop}\\EGTRAIN"
    if sorted(params.get("name", "") for _, params in entries) != [desktop, programs]:
        problems.append(f"icons: the only entries are {programs} and {desktop}")
    for number, params in entries:
        if params.get("filename") != PROGRAM:
            problems.append(f"icons: line {number}: Filename must be {PROGRAM}")
        if params.get("workingdir") != "{app}":
            problems.append(f"icons: line {number}: WorkingDir must be {{app}}")
        if params.get("name") == programs and "tasks" in params:
            problems.append(f"icons: line {number}: the Start Menu entry must not depend on a task")
        if params.get("name") == desktop and params.get("tasks") != "desktopicon":
            problems.append(f"icons: line {number}: the desktop shortcut must depend on the task desktopicon")


def check_registry(entries, problems):
    """The entries are the file type of .egscene for the current user, all removed on uninstall, and nothing else."""
    found = []
    for number, params in entries:
        subkey = params.get("subkey", "")
        if params.get("root", "").upper() != "HKCU":
            problems.append(f"registry: line {number}: Root must be HKCU")
        if params.get("valuetype", "").lower() != "string" or params.get("valuename", "") != "":
            problems.append(f"registry: line {number}: {subkey} must set its default value as a string")
        if subkey not in ASSOCIATION:
            problems.append(f"registry: line {number}: {subkey} is not part of the file type of .egscene")
            continue
        if subkey in found:
            problems.append(f"registry: line {number}: {subkey} is set twice")
        found.append(subkey)
        data, flags = ASSOCIATION[subkey]
        if data is None and not params.get("valuedata"):
            problems.append(f"registry: line {number}: {subkey} needs a display name")
        if data is not None and params.get("valuedata") != data:
            problems.append(f"registry: line {number}: ValueData of {subkey} must be {data}, not {params.get('valuedata')}")
        if flag_set(params) != flags:
            problems.append(f"registry: line {number}: Flags of {subkey} must be {' '.join(sorted(flags))}")
    for subkey in ASSOCIATION:
        if subkey not in found:
            problems.append(f"registry: there is no entry for {subkey}")


def check_readme(read, problems):
    data = read(README)
    if data is None:
        problems.append(f"readme: {README} is missing")
        return
    text = data.decode("utf-8", errors="replace")
    for needed in ("/DVersion", "/DPayload", MARKER):
        if needed not in text:
            problems.append(f"readme: {README} does not name {needed}")


def check_script(text, read=read_repository_file):
    """The problems of the script text, each starting with the name of its rule. An empty list means the text is fine.

    read(path) gives the bytes of a file below the repository root, or None. Tests pass another function to change
    one of the other files."""
    problems = []
    check_text(text, problems)
    directives, sections = split_text(text, problems)
    check_directives(directives, problems)
    check_braces(sections, problems)
    values = check_setup(sections.get("setup", []), read, problems)
    check_license(values, read, problems)
    check_output_name(values, read, problems)
    entries = {
        name: [(number, parse_entry(line, number, problems)) for number, line in sections.get(name, [])]
        for name in SECTIONS
        if name != "setup"
    }
    check_tasks(entries["tasks"], problems)
    check_files(entries["files"], read, problems)
    check_install_delete(entries["installdelete"], problems)
    check_icons(entries["icons"], problems)
    check_registry(entries["registry"], problems)
    check_readme(read, problems)
    return problems


def mutate(text, old, new):
    """Replaces the first old in text by new. It fails when old is not there, so a renamed line cannot make a test pass."""
    if old not in text:
        raise AssertionError(f"{old!r} is not in the script text")
    return text.replace(old, new, 1)


def with_lf(text):
    return "\n".join(text.splitlines()) + "\n"


def with_crlf(text):
    return "\n".join(text.splitlines()).replace("\n", "\r\n") + "\r\n"


def stand_in(overrides):
    """A read function that answers for the paths in overrides (None means no such file) and reads the repository otherwise."""
    return lambda path: overrides[path] if path in overrides else read_repository_file(path)


# The lines of the real script that the cases below change.
TASK_LINE = 'Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked\n'
PAYLOAD_LINE = 'Source: "{#Payload}\\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs\n'
MARKER_LINE = 'Source: "installed-by-setup.ini"; DestDir: "{app}"; Flags: ignoreversion\n'
SCENES_LINE = 'Type: filesandordirs; Name: "{app}\\Scenes"\n'
PROGRAMS_LINE = 'Name: "{userprograms}\\EGTRAIN"; Filename: "{app}\\QEGTRAIN.exe"; WorkingDir: "{app}"\n'
DESKTOP_LINE = 'Name: "{userdesktop}\\EGTRAIN"; Filename: "{app}\\QEGTRAIN.exe"; WorkingDir: "{app}"; Tasks: desktopicon\n'
EXTENSION_LINE = (
    'Root: HKCU; Subkey: "Software\\Classes\\.egscene"; ValueType: string; ValueName: ""; ValueData: "EGTRAIN.Scene"; '
    "Flags: uninsdeletevalue uninsdeletekeyifempty\n"
)
PROG_ID_LINE = (
    'Root: HKCU; Subkey: "Software\\Classes\\EGTRAIN.Scene"; ValueType: string; ValueName: ""; ValueData: "EGTRAIN case"; '
    "Flags: uninsdeletekey\n"
)
ICON_LINE = (
    'Root: HKCU; Subkey: "Software\\Classes\\EGTRAIN.Scene\\DefaultIcon"; ValueType: string; ValueName: ""; '
    'ValueData: "{app}\\QEGTRAIN.exe,0"; Flags: uninsdeletekey\n'
)
COMMAND_LINE = (
    'Root: HKCU; Subkey: "Software\\Classes\\EGTRAIN.Scene\\shell\\open\\command"; ValueType: string; ValueName: ""; '
    'ValueData: """{app}\\QEGTRAIN.exe"" ""%1"""; Flags: uninsdeletekey\n'
)


class InstallerScriptTests(unittest.TestCase):
    """The script, the marker file, the README and the icon as they are in the repository."""

    @classmethod
    def setUpClass(cls):
        data = read_repository_file(SCRIPT)
        if data is None:
            raise AssertionError(f"{SCRIPT} is missing")
        cls.text = script_text(data)

    def test_script_has_no_problem(self):
        self.assertEqual(check_script(self.text), [])

    def test_crlf_line_ends_have_no_problem(self):
        # A checkout on Windows may already have CRLF. Joining the lines first gives the same input either way.
        crlf = "\n".join(self.text.splitlines()).replace("\n", "\r\n")
        self.assertEqual(check_script(crlf), [])
        self.assertEqual(check_script(crlf + "\r\n"), [])

    def test_marker_is_one_comment_line(self):
        text = script_text(read_repository_file(INSTALLER + "/" + MARKER) or b"")
        self.assertTrue(text.isascii())
        lines = text.splitlines()
        self.assertEqual(len(lines), 1)
        self.assertTrue(lines[0].startswith(";"))

    def test_updater_pattern_for_the_installer(self):
        source = read_repository_file(RELEASE_INFO).decode("utf-8")
        self.assertEqual(ASSET_PATTERN.findall(source), ["EGTRAIN-Setup-%1-x64.exe"])


class MutationTests(unittest.TestCase):
    """One change to the real script per case. check_script must report a problem of the rule that the case names."""

    def setUp(self):
        self.text = with_lf(script_text(read_repository_file(SCRIPT)))

    def assertReported(self, rule, text, part="", read=read_repository_file):
        problems = check_script(text, read)
        self.assertTrue(
            any(problem.startswith(rule + ":") and part in problem for problem in problems),
            f"expected a {rule} problem that names {part!r}, got {problems}",
        )

    def check_cases(self, cases):
        for rule, part, old, new in cases:
            with self.subTest(rule=rule, old=old, new=new):
                self.assertReported(rule, mutate(self.text, old, new), part)

    def test_ascii(self):
        self.check_cases([
            ("ascii", "00E9", "; EGTRAIN installer for Windows.", "; EGTRAIN installer for Windows \u00e9."),
            ("ascii", "2014", "It installs for", "It \u2014 installs for"),
            ("ascii", "2013", "Version=X.Y.Z.", "Version=X\u2013Y."),
        ])
        self.assertReported("ascii", script_text(read_repository_file(SCRIPT) + b"; \xe2\x80\x94\n"), "FFFD")

    def test_nothing_under_the_machine_hive(self):
        machine = mutate(self.text, EXTENSION_LINE, EXTENSION_LINE.replace("HKCU", "HKLM"))
        self.assertReported("hklm", machine)
        self.assertReported("registry", machine, "HKCU")
        self.assertReported("hklm", self.text + "; hkey_local_machine\n")
        for root in ("HKA", "HKCR"):
            with self.subTest(root=root):
                self.assertReported("registry", mutate(self.text, ICON_LINE, ICON_LINE.replace("HKCU", root)), "HKCU")

    def test_no_parent_folder(self):
        self.check_cases([
            ("parent-path", "", PROGRAMS_LINE, PROGRAMS_LINE.replace("{userprograms}\\EGTRAIN", "{userprograms}\\..\\EGTRAIN")),
            ("parent-path", "", PROGRAMS_LINE, PROGRAMS_LINE.replace("{app}\\QEGTRAIN.exe", "{app}\\..\\QEGTRAIN.exe")),
            ("parent-path", "", PAYLOAD_LINE, PAYLOAD_LINE.replace('"{app}"', '"{app}\\.."')),
            ("parent-path", "", "SetupIconFile=..\\EGTRAIN", "SetupIconFile=..\\..\\EGTRAIN"),
            ("parent-path", "", "; docs\\development\\windows-installer.md", "; docs\\..\\development\\windows-installer.md"),
        ])

    def test_sections(self):
        for name in ("Code", "Run", "UninstallRun", "UninstallDelete", "Languages", "Dirs", "Components", "Types", "code"):
            with self.subTest(section=name):
                self.assertReported("section", self.text + f"\n[{name}]\nName: x\n", name)

    def test_syntax(self):
        self.check_cases([
            ("syntax", "line", TASK_LINE, TASK_LINE.replace('shortcut"', "shortcut")),
            ("syntax", "line", TASK_LINE, TASK_LINE.replace("Flags: unchecked", "Flags unchecked")),
            ("syntax", "line", TASK_LINE, TASK_LINE.replace("Flags: unchecked", "Flags: unchecked; Flags: unchecked")),
            ("syntax", "outside", "[Setup]\n", "Stray: 1\n[Setup]\n"),
            ("syntax", "[Setup]", "WizardStyle=modern", "WizardStyle modern"),
        ])

    def test_constants(self):
        self.check_cases([
            ("constant", "commonappdata", PROGRAMS_LINE, PROGRAMS_LINE.replace('WorkingDir: "{app}"', 'WorkingDir: "{commonappdata}"')),
            ("constant", "autopf", "DefaultDirName={userpf}", "DefaultDirName={autopf}"),
            ("constant", "commonpf", "DefaultDirName={userpf}", "DefaultDirName={commonpf}"),
            ("constant", "pf", "DefaultDirName={userpf}", "DefaultDirName={pf}"),
            ("constant", "sys", MARKER_LINE, MARKER_LINE.replace('"{app}"', '"{sys}"')),
            ("constant", "brace", PROGRAMS_LINE, PROGRAMS_LINE.replace("{userprograms}", "{{userprograms}")),
            ("constant", "brace", PROGRAMS_LINE, PROGRAMS_LINE.replace("{userprograms}", "{userprograms")),
        ])

    def test_macros(self):
        version_guard = "#ifndef Version\n#error Pass the release version with /DVersion=X.Y.Z.\n#endif\n"
        self.check_cases([
            ("macro", "Other", "AppVersion={#Version}", "AppVersion={#Other}"),
            ("macro", "PayloadCheck", PAYLOAD_LINE, PAYLOAD_LINE.replace("; Flags", "; Check: {#PayloadCheck}; Flags")),
            ("macro", "#ifndef Payload", "#ifndef Payload\n", ""),
            ("macro", "#ifndef Version", version_guard, ""),
            ("macro", "/DPayload", "with /DPayload=<directory>.", "with the payload."),
            ("macro", "#ifndef Payload", "#error Pass the assembled Windows package directory with /DPayload=<directory>.\n#endif\n", "#endif\n"),
        ])

    def test_directives(self):
        self.check_cases([
            ("directive", "#include", "[Setup]\n", '#include "other.iss"\n[Setup]\n'),
            ("directive", "#define", "[Setup]\n", "#define Version 1.0.0\n[Setup]\n"),
            ("directive", "#warning", "#error Pass the release version", "#warning Pass the release version"),
        ])

    def test_setup_values_are_pinned(self):
        for key, value in SETUP_PINS:
            for old, new in ((f"{key}={value}\n", ""), (f"{key}={value}", f"{key}={value}x")):
                with self.subTest(key=key, old=old):
                    self.assertReported("setup-pin", mutate(self.text, old, new), key)

    def test_setup_values_that_widen_the_install(self):
        self.check_cases([
            ("setup-pin", "PrivilegesRequired", "PrivilegesRequired=lowest\n", ""),
            ("setup-pin", "PrivilegesRequired", "PrivilegesRequired=lowest", "PrivilegesRequired=admin"),
            ("setup-pin", "DefaultDirName", "DefaultDirName={userpf}", "DefaultDirName={autopf}"),
            ("setup-pin", "DefaultDirName", "DefaultDirName={userpf}", "DefaultDirName={commonpf}"),
            ("setup-pin", "ArchitecturesAllowed", "ArchitecturesAllowed=x64compatible", "ArchitecturesAllowed=x64"),
            ("setup-pin", "CloseApplications", "CloseApplications=yes\n", ""),
            ("setup-duplicate", "privilegesrequired", "PrivilegesRequired=lowest\n", "PrivilegesRequired=lowest\nPrivilegesRequired=admin\n"),
        ])
        for key in SETUP_FORBIDDEN:
            with self.subTest(key=key):
                self.assertReported("setup-forbidden", mutate(self.text, "[Setup]\n", f"[Setup]\n{key}=yes\n"), key)
        self.assertReported("setup-forbidden", mutate(self.text, "[Setup]\n", "[Setup]\nuninstallable=no\n"), "Uninstallable")

    def test_app_id(self):
        self.check_cases([
            ("app-id", "AppId", "D3963171F664}", "D3963171F665}"),
            ("app-id", "AppId", "AppId={{", "AppId={"),
            ("app-id", "AppId", "; The AppId names", "; {{16796959-230D-4A37-879B-D3963171F664} The AppId names"),
            ("app-id", "AppId", "16796959-230D-4A37-879B-D3963171F664}", "16796959-230D-4A37-879B-D3963171F66}"),
        ])

    def test_output_name(self):
        self.check_cases([
            ("asset-name", "x86", "-{#Version}-x64", "-{#Version}-x86"),
            ("asset-name", "Setup.exe", "-{#Version}-x64", ""),
            ("asset-name", "10.20.30", "OutputBaseFilename=EGTRAIN-Setup-{#Version}", "OutputBaseFilename=EGTRAIN-Setup-1.2.3"),
        ])
        for source in (b"constexpr AssetPattern kAssetPatterns[] = {};", None):
            self.assertReported("asset-name", self.text, "not found", stand_in({RELEASE_INFO: source}))
        other = b'{"windows-x64-installer", "EGTRAIN-Setup-%1-x86.exe"}'
        self.assertReported("asset-name", self.text, "x86", stand_in({RELEASE_INFO: other}))

    def test_icon_file(self):
        far = "..\\" * 6
        self.check_cases([
            ("icon-file", "missing.ico", "app\\egtrain.ico", "app\\missing.ico"),
            ("icon-file", far, "SetupIconFile=..\\EGTRAIN", "SetupIconFile=" + far + "EGTRAIN"),
        ])
        icon = "EGTRAIN/QEGTRAIN/resources/app/egtrain.ico"
        self.assertReported("icon-file", self.text, "egtrain.ico", stand_in({icon: None}))

    def test_license_page(self):
        self.check_cases([
            ("setup-pin", "LicenseFile", "LicenseFile=..\\LICENSE\n", ""),
            ("setup-pin", "LicenseFile", "LicenseFile=..\\LICENSE", "LicenseFile=..\\README.md"),
            ("license", "LicenseFile must name", "LicenseFile=..\\LICENSE", "LicenseFile=..\\README.md"),
            ("parent-path", "", "LicenseFile=..\\LICENSE", "LicenseFile=..\\..\\LICENSE"),
            ("setup-duplicate", "licensefile", "LicenseFile=..\\LICENSE\n", "LicenseFile=..\\LICENSE\nLicenseFile=..\\LICENSE\n"),
        ])
        self.assertReported("license", self.text, "GPL-3.0", stand_in({LICENSE: None}))
        self.assertReported("license", self.text, "GPL-3.0", stand_in({LICENSE: b"The MIT License"}))

    def test_the_package_holds_the_license(self):
        workflow = read_repository_file(PACKAGE_WORKFLOW).decode("utf-8")
        copy = LICENSE_COPY.search(workflow).group(0).strip()
        check = LICENSE_CHECK.search(workflow).group(0).strip()
        cases = (
            ("copy", workflow.replace(copy, ""), "copy"),
            ("copy", workflow.replace(copy, copy.replace("LICENSE", "README.md")), "copy"),
            ("check", workflow.replace(check, ""), "check"),
            ("job", workflow.replace("  package-windows:", "  package-other:", 1), "not found"),
        )
        for name, changed, part in cases:
            with self.subTest(name=name):
                self.assertReported("license", self.text, part, stand_in({PACKAGE_WORKFLOW: changed.encode("utf-8")}))
        self.assertReported("license", self.text, "not found", stand_in({PACKAGE_WORKFLOW: None}))
        # A copy in another job does not count.
        macos = workflow.replace(copy, "").replace("  package-macos:\n", "  package-macos:\n      " + copy + "\n", 1)
        self.assertReported("license", self.text, "copy", stand_in({PACKAGE_WORKFLOW: macos.encode("utf-8")}))

    def test_payload_entry(self):
        self.check_cases([
            ("files", "payload", PAYLOAD_LINE, PAYLOAD_LINE.replace(" recursesubdirs", "")),
            ("files", "payload", PAYLOAD_LINE, PAYLOAD_LINE.replace("ignoreversion ", "")),
            ("files", "exactly one entry must install {#Payload}", PAYLOAD_LINE, PAYLOAD_LINE.replace("}\\*", "}\\bin\\*")),
            ("files", "exactly one entry must install {#Payload}", PAYLOAD_LINE, ""),
            ("files", "exactly one entry must install {#Payload}", PAYLOAD_LINE, PAYLOAD_LINE * 2),
        ])

    def test_marker_entry(self):
        self.check_cases([
            ("files", "installed-by-setup.ini into", MARKER_LINE, ""),
            ("files", "installed-by-setup.ini into", MARKER_LINE, MARKER_LINE * 2),
            ("files", "installed-by-setup.ini into", MARKER_LINE, MARKER_LINE.replace('"{app}"', '"{app}\\data"')),
            ("files", "setup.txt", MARKER_LINE, MARKER_LINE.replace("setup.ini", "setup.txt")),
        ])
        marker_path = INSTALLER + "/" + MARKER
        for content in (b"", b" \r\n", None):
            with self.subTest(content=content):
                self.assertReported("files", self.text, "missing or empty", stand_in({marker_path: content}))

    def test_files_come_from_the_package_or_the_script_directory(self):
        self.check_cases([
            ("files", "external", MARKER_LINE, MARKER_LINE.replace("ignoreversion", "ignoreversion external")),
            ("files", "external", PAYLOAD_LINE, PAYLOAD_LINE.replace("createallsubdirs", "createallsubdirs download")),
            ("files", "DestDir", MARKER_LINE, MARKER_LINE.replace('"{app}"', '"C:\\EGTRAIN"')),
            ("files", "DestDir", MARKER_LINE, MARKER_LINE.replace('DestDir: "{app}"; ', "")),
            ("files", "DestDir", MARKER_LINE, MARKER_LINE.replace('"{app}"', '"{appx}"')),
            ("files", "Source", MARKER_LINE, MARKER_LINE.replace('"installed', '"..\\installed')),
            ("files", "Source", MARKER_LINE, MARKER_LINE.replace('"installed', '"{tmp}\\installed')),
        ])

    def test_the_qt5_files_of_an_older_installation_are_deleted(self):
        library_line = 'Type: files; Name: "{app}\\Qt5*.dll"\n'
        self.check_cases([
            ("install-delete", "Qt5*.dll", library_line, ""),
            ("install-delete", "not {app}\\imageformats\\qgif.dll", library_line,
             library_line + 'Type: files; Name: "{app}\\imageformats\\qgif.dll"\n'),
        ])

    def test_only_the_scenes_folder_is_deleted_before_an_install(self):
        self.check_cases([
            ("install-delete", "{app}\\*", SCENES_LINE, SCENES_LINE + SCENES_LINE.replace("Scenes", "*")),
            ("install-delete", "not {app}", SCENES_LINE, SCENES_LINE + SCENES_LINE.replace("\\Scenes", "")),
            ("install-delete", "{app}\\Scenes\\old", SCENES_LINE, SCENES_LINE.replace("Scenes", "Scenes\\old")),
            ("install-delete", "{userdocs}", SCENES_LINE, SCENES_LINE + "Type: files; Name: \"{userdocs}\\x\"\n"),
            ("install-delete", "must be deleted", SCENES_LINE, ""),
            ("install-delete", "must be deleted", SCENES_LINE, SCENES_LINE.replace("Scenes", "scenes")),
        ])

    def test_desktop_task(self):
        self.check_cases([
            ("tasks", "unchecked", TASK_LINE, TASK_LINE.replace("; Flags: unchecked", "")),
            ("tasks", "only task", TASK_LINE, TASK_LINE.replace('"desktopicon"', '"desktop"')),
            ("tasks", "only task", TASK_LINE, TASK_LINE + TASK_LINE.replace('"desktopicon"', '"other"')),
            ("tasks", "only task", TASK_LINE, ""),
        ])

    def test_shortcuts(self):
        self.check_cases([
            ("icons", "desktop shortcut", DESKTOP_LINE, DESKTOP_LINE.replace("; Tasks: desktopicon", "")),
            ("icons", "desktop shortcut", DESKTOP_LINE, DESKTOP_LINE.replace("Tasks: desktopicon", "Tasks: other")),
            ("icons", "Start Menu", PROGRAMS_LINE, PROGRAMS_LINE.replace('"{app}"\n', '"{app}"; Tasks: desktopicon\n')),
            ("icons", "Filename", PROGRAMS_LINE, PROGRAMS_LINE.replace("QEGTRAIN.exe", "scene_tool.exe")),
            ("icons", "Filename", PROGRAMS_LINE, PROGRAMS_LINE.replace("{app}\\QEGTRAIN.exe", "{app}")),
            ("icons", "WorkingDir", PROGRAMS_LINE, PROGRAMS_LINE.replace('; WorkingDir: "{app}"', "")),
            ("icons", "WorkingDir", DESKTOP_LINE, DESKTOP_LINE.replace('WorkingDir: "{app}"', 'WorkingDir: "{userdocs}"')),
            ("icons", "only entries", PROGRAMS_LINE, ""),
            ("icons", "only entries", PROGRAMS_LINE, PROGRAMS_LINE * 2),
            ("icons", "only entries", PROGRAMS_LINE, PROGRAMS_LINE + PROGRAMS_LINE.replace("userprograms", "userstartup")),
            ("icons", "only entries", PROGRAMS_LINE, PROGRAMS_LINE.replace("userprograms", "userstartmenu")),
        ])

    def test_file_type_is_removed_on_uninstall_and_leaves_other_values_of_the_extension(self):
        self.check_cases([
            ("registry", "Flags", EXTENSION_LINE, EXTENSION_LINE.replace("uninsdeletevalue uninsdeletekeyifempty", "uninsdeletekey")),
            ("registry", "Flags", EXTENSION_LINE, EXTENSION_LINE.replace("; Flags: uninsdeletevalue uninsdeletekeyifempty", "")),
            ("registry", "Flags", EXTENSION_LINE, EXTENSION_LINE.replace(" uninsdeletekeyifempty", "")),
            ("registry", "Flags", EXTENSION_LINE, EXTENSION_LINE.replace("uninsdeletevalue", "createvalueifdoesntexist")),
            ("registry", "Flags", PROG_ID_LINE, PROG_ID_LINE.replace("uninsdeletekey", "uninsdeletevalue")),
            ("registry", "Flags", ICON_LINE, ICON_LINE.replace("; Flags: uninsdeletekey", "")),
            ("registry", "Flags", COMMAND_LINE, COMMAND_LINE.replace("; Flags: uninsdeletekey", "; Flags: createvalueifdoesntexist")),
        ])

    def test_file_type_is_per_user_and_opens_the_program_with_the_file(self):
        self.check_cases([
            ("registry", "must be EGTRAIN.Scene", EXTENSION_LINE, EXTENSION_LINE.replace('"EGTRAIN.Scene"', '"EGTRAIN"')),
            ("registry", "QEGTRAIN.exe,0", ICON_LINE, ICON_LINE.replace("QEGTRAIN.exe,0", "QEGTRAIN.exe,1")),
            ("registry", "QEGTRAIN.exe,0", ICON_LINE, ICON_LINE.replace("QEGTRAIN.exe", "scene_tool.exe")),
            ("registry", "QEGTRAIN.exe", COMMAND_LINE, COMMAND_LINE.replace("QEGTRAIN.exe", "scene_tool.exe")),
            ("registry", "%1", COMMAND_LINE, COMMAND_LINE.replace("%1", "%2")),
            ("registry", "%1", COMMAND_LINE, COMMAND_LINE.replace(' ""%1"""', '"')),
            ("registry", "QEGTRAIN.exe", COMMAND_LINE, COMMAND_LINE.replace('"""{app}', '"{app}').replace('exe"" ""', 'exe ""')),
            ("registry", "display name", PROG_ID_LINE, PROG_ID_LINE.replace('"EGTRAIN case"', '""')),
            ("registry", "default value", ICON_LINE, ICON_LINE.replace('ValueName: ""', 'ValueName: "Icon"')),
            ("registry", "default value", ICON_LINE, ICON_LINE.replace("ValueType: string", "ValueType: dword")),
        ])

    def test_file_type_is_the_extension_egscene_alone(self):
        zip_line = EXTENSION_LINE.replace(".egscene", ".zip")
        self.check_cases([
            ("registry", ".zip", EXTENSION_LINE, EXTENSION_LINE + zip_line),
            ("registry", ".zip", EXTENSION_LINE, zip_line),
            ("registry", "Software\\EGTRAIN", ICON_LINE, ICON_LINE + ICON_LINE.replace("Classes\\EGTRAIN.Scene\\DefaultIcon", "EGTRAIN")),
            ("registry", "EGTRAIN.Other", PROG_ID_LINE, PROG_ID_LINE + PROG_ID_LINE.replace("EGTRAIN.Scene", "EGTRAIN.Other")),
            ("registry", "no entry for", EXTENSION_LINE, ""),
            ("registry", "no entry for", PROG_ID_LINE, ""),
            ("registry", "no entry for", ICON_LINE, ""),
            ("registry", "no entry for", COMMAND_LINE, ""),
            ("registry", "set twice", ICON_LINE, ICON_LINE * 2),
        ])

    def test_readme_names_the_parameters_and_the_marker(self):
        self.assertReported("readme", self.text, "missing", stand_in({README: None}))
        readme = read_repository_file(README).decode("utf-8")
        for needed in ("/DVersion", "/DPayload", MARKER):
            with self.subTest(needed=needed):
                self.assertIn(needed, readme)
                changed = stand_in({README: readme.replace(needed, "x").encode("utf-8")})
                self.assertReported("readme", self.text, needed, changed)

    def test_a_mutation_is_found_with_crlf_line_ends(self):
        crlf = with_crlf(self.text)
        self.assertEqual(check_script(crlf), [])
        self.assertReported("setup-pin", mutate(crlf, "PrivilegesRequired=lowest\r\n", "PrivilegesRequired=admin\r\n"), "PrivilegesRequired")
        self.assertReported("section", crlf + "[Run]\r\nFilename: x\r\n", "Run")

    def test_mutate_refuses_a_text_that_is_not_there(self):
        with self.assertRaises(AssertionError):
            mutate(self.text, "PrivilegesRequired=highest", "PrivilegesRequired=admin")


class AcceptedChangeTests(unittest.TestCase):
    """Changes that the rules must let through, so that they do not forbid more than they say."""

    def setUp(self):
        self.text = with_lf(script_text(read_repository_file(SCRIPT)))

    def assertFine(self, text, read=read_repository_file):
        self.assertEqual(check_script(text, read), [])

    def test_comments_are_not_read_as_entries(self):
        comments = "; [Code]\n; Source: x; DestDir: {commonappdata}; Flags: external\n; PrivilegesRequiredOverridesAllowed=dialog\n"
        self.assertFine(self.text + comments)

    def test_a_semicolon_inside_quotes_does_not_split_a_value(self):
        self.assertFine(mutate(self.text, TASK_LINE, TASK_LINE.replace("Create a &desktop", "Create a; &desktop")))

    def test_names_are_not_case_sensitive(self):
        text = mutate(self.text, "[Files]", "[files]")
        self.assertFine(mutate(text, "PrivilegesRequired=lowest", "privilegesrequired=lowest"))

    def test_the_display_name_is_free_text(self):
        self.assertFine(mutate(self.text, PROG_ID_LINE, PROG_ID_LINE.replace("EGTRAIN case", "EGTRAIN Case File")))

    def test_flags_may_come_in_another_order_and_the_root_in_another_case(self):
        flags = EXTENSION_LINE.replace("uninsdeletevalue uninsdeletekeyifempty", "uninsdeletekeyifempty uninsdeletevalue")
        self.assertFine(mutate(self.text, EXTENSION_LINE, flags))
        self.assertFine(mutate(self.text, EXTENSION_LINE, EXTENSION_LINE.replace("HKCU", "hkcu")))

    def test_the_default_value_may_be_named_by_leaving_out_the_value_name(self):
        self.assertFine(mutate(self.text, ICON_LINE, ICON_LINE.replace('ValueName: ""; ', "")))

    def test_another_file_of_the_installer_directory_can_be_installed(self):
        script = MARKER_LINE.replace("installed-by-setup.ini", "egtrain.iss").replace('"{app}"', '"{app}\\docs"')
        self.assertFine(mutate(self.text, MARKER_LINE, MARKER_LINE + script))

    def test_the_updater_entry_may_be_written_with_other_spacing(self):
        source = b'{ "windows-x64-installer" ,\n\t"EGTRAIN-Setup-%1-x64.exe" }'
        self.assertFine(self.text, stand_in({RELEASE_INFO: source}))


if __name__ == "__main__":
    unittest.main()
