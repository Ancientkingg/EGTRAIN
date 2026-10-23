#!/usr/bin/env python3
"""Rehearse the clean-install GUI steps 1 to 9 of the release testing checklist on the extracted Linux AppImage.

usage: linux_rehearsal.py PACKAGE_DIR BUNDLE [--work DIR] [--evidence DIR] [--expected-version X.Y.Z] [--last-step N]

PACKAGE_DIR is the extracted AppImage (the directory with AppRun) and BUNDLE is the downloaded .egscene file. The script
runs on a Linux desktop session with an X display and a D-Bus session bus, which the job provides with
xvfb-run and dbus-run-session. It drives the application as a student does, through its menus and dialogs, with no test
hook: the release package has none. The widgets are found and operated through the accessibility interface AT-SPI
(python3-gi with gir1.2-atspi-2.0). The only synthetic keystrokes are the ones that type into a text field and into a
table cell, sent with xdotool, and the screen is read with ImageMagick.

Each step prints one line, STEP n PASS or STEP n FAIL with the detail. The script stops at the first failed step, names
it and exits with 1. A screenshot of every step, the failure tree of the application, the output of the application and
the log of the script go to the evidence directory. A missing tool or display exits with 2 before the first step.

1. Start AppRun with a new home directory and no QEGTRAIN_ variable, close the case chooser, answer the question about
   automatic update checks, read the version of About EGTRAIN, and check that the toolbar icon buttons are not blank.
2. File > Open Case... on the downloaded bundle, after a byte copy of it is taken and its modification time noted.
3. The network is drawn, and View > Loaded Data shows the identity, the category counts, the scenarios, the result
   readiness and, after View > Advanced Details is ticked, the versions.
4. Type a new description into a text field of the Case Settings editor and into the scenario editor, type into a table
   cell, and check that the run review names the scenario.
5. File > Save As... to a working copy in another folder. The downloaded bundle stays as it was.
6. Simulation > Run and Run simulation in the review window, from the working copy, to the end of the run.
7. Open the timetable table with its planned and simulated values, the delay, the speed and the blocking-time results.
8. Export a CSV file and a PNG file with the buttons of Run Results.
9. File > Quit, start the package again and reopen the working copy. The edits are in it, and the downloaded bundle is
   still equal to its copy.

The checks that the toolbar icons are drawn and that typed text reaches a text field and a table cell are part of the
steps 1 and 4. The buttons Zoom In and Zoom Out of the toolbar show an icon and no text, so a flat area of one colour
means that the SVG icon is not drawn.

Not covered: dragging a file from a file manager (row R2), a real desktop session and the questions that a desktop
environment asks at the first start.
"""
import argparse
import ctypes
import filecmp
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import traceback
import zipfile
from dataclasses import dataclass
from pathlib import Path

STEPS = (
    (1, "Start the package"),
    (2, "Open the downloaded case"),
    (3, "Check the network and Loaded Data"),
    (4, "Edit the case and a scenario"),
    (5, "Save a working copy"),
    (6, "Run the simulation"),
    (7, "Open the result views"),
    (8, "Export a CSV and a PNG file"),
    (9, "Quit, start again and reopen the working copy"),
)
STARTUP_TIMEOUT_SECONDS = 240
UI_TIMEOUT_SECONDS = 60
RUN_TIMEOUT_SECONDS = 600
QUIT_TIMEOUT_SECONDS = 60
POLL_SECONDS = 0.25
# Milliseconds that a call of the accessibility interface may take. An action that opens a modal dialog does not answer
# before the dialog is closed, so those actions wait only for the short time.
REPLY_MILLISECONDS = 5000
MODAL_REPLY_MILLISECONDS = 1000

CASE_NOTE = "Case description typed in the Linux rehearsal"
SCENARIO_NOTE = "Scenario description typed in the Linux rehearsal"
# The ID of the first track is typed over and put back, so that the table edit does not change the case.
TRACK_SUFFIX = "9"

# The toolbar buttons that show an icon and no text. A flat area means that the icon is not drawn. The other buttons have a
# text, so their area is never flat.
TOOLBAR_ICON_BUTTONS = ("Zoom In", "Zoom Out")
MIN_ICON_COLOURS = 2
MIN_NETWORK_COLOURS = 16
LOADED_DATA_COLUMNS = 4
LOADED_DATA_CATEGORIES = ("Infrastructure", "Stations", "Timetable", "Rolling stock", "Signalling", "Scenarios")
LOADED_DATA_CASE = "Case"
LOADED_DATA_RUN = "Run"
# The version rows show only with advanced details. The application stores that choice when its View entry is ticked, so
# the rest of the rehearsal and the second start of step 9 keep it.
LOADED_DATA_VERSIONS = ("Format version", "Saved with", "Bundle version")
# The View entry that switches advanced details on, in any capitals.
ADVANCED_DETAILS = r"(?i)^advanced\b.*details$"
# The line of the run summary when the run has finished. With Advanced details the summary has more lines after it.
RUN_COMPLETED = r"(?m)^Status: Completed$"
TIMETABLE_COLUMNS = ("Planned arrival", "Planned departure", "Simulated arrival", "Simulated departure")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
MIN_PNG_BYTES = 1000


class StepFailed(Exception):
    pass


def environment_error(message: str):
    """Reports a missing tool, display or bus before the first step and exits with 2."""
    print(message, file=sys.stderr)
    raise SystemExit(2)


def format_step_line(number: int, passed: bool, detail: str) -> str:
    text = " ".join(detail.split())
    return f"STEP {number} {'PASS' if passed else 'FAIL'}" + (f" {text}" if text else "")


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    pixels: bytes  # three bytes per pixel (red, green, blue), row by row


def parse_ppm(data: bytes) -> Image:
    """Reads a binary PPM (P6) image with eight bits for each colour."""
    tokens, position = [], 0
    while len(tokens) < 4:
        while position < len(data) and data[position:position + 1].isspace():
            position += 1
        if data[position:position + 1] == b"#":
            position = data.find(b"\n", position)
            if position < 0:
                raise ValueError("the PPM header ends inside a comment")
            continue
        start = position
        while position < len(data) and not data[position:position + 1].isspace():
            position += 1
        if start == position:
            raise ValueError("the PPM header is too short")
        tokens.append(data[start:position])
    position += 1
    if tokens[0] != b"P6" or tokens[3] != b"255":
        raise ValueError("not a binary PPM with eight bits for each colour")
    width, height = int(tokens[1]), int(tokens[2])
    pixels = data[position:position + width * height * 3]
    if width < 1 or height < 1 or len(pixels) != width * height * 3:
        raise ValueError("the PPM data does not match its size")
    return Image(width, height, pixels)


def count_colours(image: Image, x: int, y: int, width: int, height: int, limit: int = 0) -> int:
    """Counts the different colours in a region of the image, which is clipped to the image. With a limit, the count stops
    after the first row that brings it to the limit, so a count at or above the limit means that the region has that many."""
    left, top = max(x, 0), max(y, 0)
    right, bottom = min(x + width, image.width), min(y + height, image.height)
    colours = set()
    for row in range(top, bottom):
        start = (row * image.width + left) * 3
        data = image.pixels[start:start + (right - left) * 3]
        colours.update(zip(data[0::3], data[1::3], data[2::3]))
        if limit and len(colours) >= limit:
            break
    return len(colours)


def blank_icons(image: Image, buttons) -> list:
    """The names of the icon buttons, given as (name, extents), whose area has fewer than two colours."""
    return [name for name, extents in buttons if count_colours(image, *extents, limit=MIN_ICON_COLOURS) < MIN_ICON_COLOURS]


def version_of(about_text: str):
    found = re.search(r"Version\s+(\S+)", about_text)
    return found.group(1) if found else None


def chunk(items, size: int) -> list:
    return [tuple(items[start:start + size]) for start in range(0, len(items) - size + 1, size)]


def loaded_data_problems(rows, case_name: str) -> list:
    """What the Loaded Data rows lack. A row is (category, source, count, status)."""
    by_category = {}
    for category, source, count, status in rows:
        by_category.setdefault(category, (source, count, status))
    problems = []
    if by_category.get("Name", ("",))[0] != case_name:
        problems.append(f"the name of the case ({case_name})")
    problems.extend(f"the row {label}" for label in LOADED_DATA_VERSIONS if label not in by_category)
    for label in LOADED_DATA_CATEGORIES:
        entry = by_category.get(label)
        if entry is None:
            problems.append(f"the row {label}")
        elif not entry[1].isdigit() or not entry[2]:
            problems.append(f"the count and the status of {label}")
    scenarios = by_category.get("Scenarios")
    if scenarios and scenarios[1].isdigit() and int(scenarios[1]) < 1:
        problems.append("a scenario")
    if LOADED_DATA_RUN not in by_category:
        problems.append(f"the row {LOADED_DATA_RUN}")
    return problems


def timetable_problems(headers, cells) -> list:
    """What the timetable table lacks: a planned and a simulated column, and a stop with both times."""
    problems = [f"the column {name}" for name in TIMETABLE_COLUMNS if name not in headers]
    if problems:
        return problems
    rows = chunk(cells, len(headers))
    planned = headers.index("Planned departure")
    simulated = headers.index("Simulated departure")
    if not any(re.match(r"\d\d:\d\d:\d\d$", row[planned]) and re.match(r"\d\d:\d\d:\d\d$", row[simulated]) for row in rows):
        problems.append("a stop with a planned and a simulated departure time")
    return problems


def csv_problem(text: str):
    """Why a text is not the CSV file of a run, or None."""
    lines = text.splitlines()
    if not lines:
        return "it is empty"
    if "," not in lines[0]:
        return "the first line has no comma"
    if not any(line.strip() for line in lines[1:]):
        return "it has a header line and no data line"
    return None


def png_problem(data: bytes):
    """Why a byte string is not a PNG image of some size, or None."""
    if data[:8] != PNG_SIGNATURE:
        return "it does not start with the PNG signature"
    if data[12:16] != b"IHDR":
        return "it has no IHDR chunk after the signature"
    width, height = int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big")
    if width < 1 or height < 1:
        return "the image has no size"
    if len(data) < MIN_PNG_BYTES:
        return f"the file has only {len(data)} bytes"
    return None


def bundle_case_name(bundle: Path) -> str:
    """The name of the case that a bundle holds, from its scene.json."""
    with zipfile.ZipFile(bundle) as archive:
        return json.loads(archive.read("scene.json"))["name"]


def application_environment(home: Path, runtime: Path) -> dict:
    """The environment of the application: a new home, the display and the session bus, and the two switches that make Qt
    publish its widgets on the accessibility bus. No QEGTRAIN_ variable and no library or plugin path."""
    env = {"PATH": "/usr/bin:/bin", "HOME": str(home), "LANG": "C.UTF-8", "XDG_RUNTIME_DIR": str(runtime),
           "QT_ACCESSIBILITY": "1", "QT_LINUX_ACCESSIBILITY_ALWAYS_ON": "1"}
    for name in ("DISPLAY", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS"):
        if name in os.environ:
            env[name] = os.environ[name]
    return env


class XConnection:
    """A connection to the X server that stays open, so that the server keeps the property that it sets. Qt reads the address
    of the accessibility bus from that property of the root window, as it does in a desktop session."""

    def __init__(self):
        self.x11 = ctypes.CDLL("libX11.so.6")
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XDefaultRootWindow.restype = ctypes.c_ulong
        self.x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        self.x11.XInternAtom.restype = ctypes.c_ulong
        self.x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        self.x11.XChangeProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_int,
                                             ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        self.x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.display = self.x11.XOpenDisplay(None)
        if not self.display:
            environment_error("cannot open the X display " + os.environ.get("DISPLAY", "(DISPLAY is not set)"))

    def set_root_string(self, name: str, value: str) -> None:
        xa_string = 31
        root = self.x11.XDefaultRootWindow(self.display)
        atom = self.x11.XInternAtom(self.display, name.encode(), 0)
        data = value.encode()
        self.x11.XChangeProperty(self.display, root, atom, xa_string, 8, 0, data, len(data))
        self.x11.XSync(self.display, 0)

    def close(self) -> None:
        self.x11.XCloseDisplay(self.display)


Atspi = None
GLib = None
Gio = None


def load_atspi() -> None:
    global Atspi, GLib, Gio
    try:
        import gi
        gi.require_version("Atspi", "2.0")
        from gi.repository import Atspi as atspi, GLib as glib, Gio as gio
    except (ImportError, ValueError) as exc:
        environment_error(f"python3-gi with gir1.2-atspi-2.0 is needed: {exc}")
    Atspi, GLib, Gio = atspi, glib, gio
    Atspi.set_timeout(REPLY_MILLISECONDS, STARTUP_TIMEOUT_SECONDS * 1000)


def start_accessibility_bus() -> XConnection:
    """Starts the accessibility bus the way a desktop session does: the session bus starts it when it is asked for its
    address, the address goes to the root window of the display, and the bus is marked as enabled."""
    session = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    reply = session.call_sync("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress", None,
                              GLib.VariantType("(s)"), Gio.DBusCallFlags.NONE, 30000, None)
    connection = XConnection()
    connection.set_root_string("AT_SPI_BUS", reply.unpack()[0])
    enabled = GLib.Variant("(ssv)", ("org.a11y.Status", "IsEnabled", GLib.Variant("b", True)))
    session.call_sync("org.a11y.Bus", "/org/a11y/bus", "org.freedesktop.DBus.Properties", "Set", enabled, None,
                      Gio.DBusCallFlags.NONE, 10000, None)
    return connection


def children(accessible) -> list:
    try:
        count = accessible.get_child_count()
    except GLib.Error:
        return []
    found = []
    for index in range(count):
        try:
            child = accessible.get_child_at_index(index)
        except GLib.Error:
            continue
        if child is not None:
            found.append(child)
    return found


def role_of(accessible) -> str:
    try:
        return accessible.get_role_name()
    except GLib.Error:
        return ""


def name_of(accessible) -> str:
    try:
        return accessible.get_name() or ""
    except GLib.Error:
        return ""


def has_state(accessible, state) -> bool:
    try:
        return accessible.get_state_set().contains(state)
    except GLib.Error:
        return False


def is_showing(accessible) -> bool:
    return has_state(accessible, Atspi.StateType.SHOWING)


def extents_of(accessible):
    try:
        found = accessible.get_component_iface().get_extents(Atspi.CoordType.SCREEN)
    except (GLib.Error, AttributeError):
        return None
    return found.x, found.y, found.width, found.height


def walk(accessible, depth: int = 0, max_depth: int = 8):
    yield accessible, depth
    if depth < max_depth:
        for child in children(accessible):
            yield from walk(child, depth + 1, max_depth)


def matches(root, role=None, name=None, pattern=None, max_depth: int = 8):
    for accessible, _ in walk(root, 0, max_depth):
        if role is not None and role_of(accessible) != role:
            continue
        label = name_of(accessible)
        if name is not None and label != name:
            continue
        if pattern is not None and not re.search(pattern, label):
            continue
        yield accessible


def find_all(root, role=None, name=None, pattern=None, max_depth: int = 8) -> list:
    return list(matches(root, role, name, pattern, max_depth))


def find_first(root, role=None, name=None, pattern=None, max_depth: int = 8, showing: bool = False):
    return next((item for item in matches(root, role, name, pattern, max_depth) if not showing or is_showing(item)), None)


def text_of(accessible) -> str:
    try:
        return Atspi.Text.get_text(accessible, 0, Atspi.Text.get_character_count(accessible))
    except GLib.Error:
        return ""


def table_texts(table) -> tuple:
    """The column headers and the cell texts of a table, in order."""
    headers = [name_of(item) for item in find_all(table, "table column header", max_depth=2)]
    cells = [name_of(item) for item in find_all(table, "table cell", max_depth=2)]
    return headers, cells


def dump_tree(root, out, max_depth: int = 7) -> None:
    for accessible, depth in walk(root, 0, max_depth):
        extents = extents_of(accessible)
        shown = "showing" if is_showing(accessible) else "hidden"
        out.write(f"{'  ' * depth}{role_of(accessible)!r} {name_of(accessible)[:100]!r} {extents} {shown}\n")


def wait_until(check, timeout: float, what: str):
    """Calls check until it returns something other than None or False. Fails with the thing that was waited for."""
    deadline = time.monotonic() + timeout
    while True:
        found = check()
        if found:
            return found
        if time.monotonic() >= deadline:
            raise StepFailed(f"timed out after {int(timeout)} s waiting for {what}")
        time.sleep(POLL_SECONDS)


class Rehearsal:
    def __init__(self, package: Path, bundle: Path, work: Path, evidence: Path, expected_version, last_step: int):
        self.package, self.bundle, self.work, self.evidence = package, bundle, work, evidence
        self.expected_version, self.last_step = expected_version, last_step
        self.case_name = bundle_case_name(bundle)
        self.home = work / "home"
        self.runtime = work / "runtime"
        self.copies = work / "copies"
        self.exports = work / "exports"
        self.working_copy = work / "Documents" / f"{bundle.stem} working copy.egscene"
        self.process = None
        self.app = None
        self.log_file = None
        self.app_log = None
        self.screenshots = 0
        self.scenario_id = ""
        self.track_id = ""
        self.original_copy = None
        self.original_mtime = None
        self.saved_copy = None

    # Records and evidence

    def note(self, message: str, echo: bool = True) -> None:
        self.log_file.write(message + "\n")
        self.log_file.flush()
        if echo:
            print("# " + message, flush=True)

    def screenshot(self, label: str, save: bool = True):
        """Reads the display and returns it as an image. A screenshot goes to the evidence directory unless it is for a
        check only."""
        captured = subprocess.run(["import", "-silent", "-window", "root", "-depth", "8", "ppm:-"], stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
        if captured.returncode != 0:
            raise StepFailed("import could not read the screen: " + captured.stderr.decode(errors="replace")[:300])
        if save:
            self.screenshots += 1
            path = self.evidence / f"{self.screenshots:02d}-{label}.png"
            converted = subprocess.run(["convert", "ppm:-", f"png:{path}"], input=captured.stdout, stderr=subprocess.PIPE)
            if converted.returncode != 0:
                self.note("convert could not write " + path.name + ": " + converted.stderr.decode(errors="replace")[:300])
        return parse_ppm(captured.stdout)

    # The application

    def start_application(self) -> None:
        env = application_environment(self.home, self.runtime)
        self.note("variables of the application: " + " ".join(sorted(env)))
        self.app_log = (self.evidence / "application.log").open("ab")
        self.note(f"starting {self.package / 'AppRun'} in {self.home}")
        self.process = subprocess.Popen([str(self.package / "AppRun")], cwd=self.home, env=env, stdout=self.app_log,
                                        stderr=subprocess.STDOUT, start_new_session=True)

    def stop_application(self) -> None:
        """Ends the application process group that this script started, if it still runs."""
        if self.process is None:
            return
        if self.process.poll() is None:
            for sig in (signal.SIGTERM, signal.SIGKILL):
                try:
                    os.killpg(self.process.pid, sig)
                except ProcessLookupError:
                    break
                try:
                    self.process.wait(timeout=10)
                    break
                except subprocess.TimeoutExpired:
                    continue
        self.process = None

    def application(self):
        """The accessible application of the process that this script started."""
        def look():
            for candidate in children(Atspi.get_desktop(0)):
                try:
                    if candidate.get_process_id() == self.process.pid:
                        return candidate
                except GLib.Error:
                    continue
            return None
        return wait_until(look, STARTUP_TIMEOUT_SECONDS, "the application on the accessibility bus")

    def windows(self) -> list:
        return children(self.app)

    def main_frame(self):
        return next((item for item in self.windows() if role_of(item) == "frame" and name_of(item).startswith("EGTRAIN")),
                    None)

    def window(self, name=None, pattern=None):
        for item in self.windows():
            label = name_of(item)
            if role_of(item) in ("dialog", "alert", "frame") and is_showing(item) and (
                    (name is not None and label == name) or (pattern is not None and re.search(pattern, label))):
                return item
        return None

    def wait_window(self, name=None, pattern=None, timeout: float = UI_TIMEOUT_SECONDS):
        return wait_until(lambda: self.window(name, pattern), timeout, f"the window {name or pattern}")

    def wait_closed(self, name=None, pattern=None, timeout: float = UI_TIMEOUT_SECONDS) -> None:
        wait_until(lambda: self.window(name, pattern) is None, timeout, f"the window {name or pattern} to close")

    # Operating widgets

    def press(self, widget, modal: bool = False) -> None:
        """Does the first action of a widget. An action that opens a modal dialog gets no reply before the dialog closes, so
        a missing reply is no failure here: the step waits for what the action must cause."""
        action = widget.get_action_iface()
        if action is None or action.get_n_actions() < 1:
            raise StepFailed(f"{name_of(widget)!r} has no action")
        Atspi.set_timeout(MODAL_REPLY_MILLISECONDS if modal else REPLY_MILLISECONDS, STARTUP_TIMEOUT_SECONDS * 1000)
        try:
            action.do_action(0)
        except GLib.Error as exc:
            if not modal:
                self.note(f"no reply to the action of {name_of(widget)!r}: {exc}")
        finally:
            Atspi.set_timeout(REPLY_MILLISECONDS, STARTUP_TIMEOUT_SECONDS * 1000)

    def menu_item(self, name=None, pattern=None, menu=None):
        """The first entry of the menu bar with that name or pattern. With `menu` only the entries of the menu with that
        title count, so that File > Run and Simulation > Run are told apart."""
        bar = find_first(self.main_frame(), "menu bar", max_depth=1)
        root = find_first(bar, name=menu, max_depth=1) if bar and menu else bar
        item = find_first(root, "menu item", name, pattern, max_depth=4) if root else None
        if item is None:
            where = f" in the menu {menu}" if menu else ""
            raise StepFailed(f"the menu entry {name or pattern!r} is not there{where}")
        return item

    def choose(self, name: str, modal: bool = False, menu=None) -> None:
        self.press(self.menu_item(name, menu=menu), modal)

    def show_dock(self, menu_name: str, frame_name: str):
        """Shows a dock through its checkable menu entry unless it is shown, and returns its frame."""
        item = self.menu_item(menu_name)
        frame = find_first(self.main_frame(), "frame", frame_name, max_depth=1)
        if frame is None or not is_showing(frame):
            self.press(item)
        return wait_until(lambda: self.dock(frame_name), UI_TIMEOUT_SECONDS, f"the dock {frame_name}")

    def dock(self, frame_name: str):
        frame = find_first(self.main_frame(), "frame", frame_name, max_depth=1)
        return frame if frame is not None and is_showing(frame) else None

    def button(self, root, name: str, timeout: float = UI_TIMEOUT_SECONDS):
        def look():
            found = find_first(root, "push button", name, max_depth=10, showing=True)
            return found if found is not None and has_state(found, Atspi.StateType.ENABLED) else None
        return wait_until(look, timeout, f"the enabled button {name!r}")

    def set_file_name(self, dialog, path: Path) -> None:
        field = wait_until(lambda: find_first(dialog, "text", "File name:", max_depth=4), UI_TIMEOUT_SECONDS,
                           "the file name field")
        Atspi.EditableText.set_text_contents(field, str(path))
        wait_until(lambda: text_of(field) == str(path), UI_TIMEOUT_SECONDS, "the file name in the dialog")

    def give_keyboard(self, widget) -> None:
        """The display has no window manager, so the pointer goes over the widget and the window under the pointer gets the
        input focus of the display. Keys and clicks then reach that window."""
        extents = extents_of(widget)
        if extents is None or extents[2] < 1:
            raise StepFailed(f"{name_of(widget)!r} has no place on the screen")
        subprocess.run(["xdotool", "mousemove", str(extents[0] + extents[2] // 2), str(extents[1] + extents[3] // 2)],
                       check=True)
        under_pointer = subprocess.run(["xdotool", "getmouselocation", "--shell"], check=True, stdout=subprocess.PIPE,
                                       text=True).stdout
        window = re.search(r"^WINDOW=(\d+)$", under_pointer, re.MULTILINE)
        if window is None:
            raise StepFailed("there is no window under the pointer")
        subprocess.run(["xdotool", "windowfocus", window.group(1)], check=True)

    def focus(self, widget) -> None:
        """Gives the keyboard to a text field and takes the focus in its window."""
        self.give_keyboard(widget)
        widget.get_component_iface().grab_focus()
        wait_until(lambda: has_state(widget, Atspi.StateType.FOCUSED), UI_TIMEOUT_SECONDS,
                   f"the focus on {name_of(widget)!r}")

    def type_keys(self, text: str) -> None:
        subprocess.run(["xdotool", "key", "--clearmodifiers", "ctrl+a"], check=True)
        subprocess.run(["xdotool", "type", "--clearmodifiers", "--delay", "30", "--", text], check=True)

    def type_into_field(self, field, text: str) -> None:
        """Types into a line edit with real keys and reads the text back through the accessibility interface."""
        self.focus(field)
        self.type_keys(text)
        wait_until(lambda: text_of(field) == text, UI_TIMEOUT_SECONDS, f"the typed text in {name_of(field)!r}")
        subprocess.run(["xdotool", "key", "--clearmodifiers", "Return"], check=True)

    # The steps

    def step1(self) -> str:
        self.start_application()
        self.app = self.application()
        chooser = wait_until(lambda: self.window("Open a Case"), STARTUP_TIMEOUT_SECONDS, "the case chooser")
        items = [name_of(item) for item in find_all(chooser, "list item", max_depth=6)]
        bundled = items[1:items.index("Recent cases")] if "Recent cases" in items else items[1:]
        if not bundled:
            raise StepFailed("the case chooser lists no bundled case: " + repr(items))
        self.note("the case chooser lists " + ", ".join(items))
        self.screenshot("case-chooser")
        close = find_first(chooser, "push button", "Continue", max_depth=4) or self.button(chooser, "Cancel")
        self.note(f"closing the case chooser with {name_of(close)!r}")
        self.press(close)
        self.wait_closed("Open a Case")
        question = self.ask_about_updates(20)
        if question is not None:
            labels = [name_of(item) for item in find_all(question, "label", max_depth=4)]
            self.note(f"dialog {name_of(question)!r}: " + " | ".join(labels))
            self.screenshot("update-question")
            self.press(self.button(question, "Don't Check Automatically"))
            self.wait_closed(pattern="Automatically Check")
        else:
            self.note("no question about update checks appeared")
        self.choose("About EGTRAIN", modal=True)
        about = self.wait_window("About EGTRAIN")
        about_text = " ".join(name_of(item) for item in find_all(about, "label", max_depth=6))
        version = version_of(about_text)
        self.note("About EGTRAIN: " + about_text)
        self.screenshot("about")
        if version is None:
            raise StepFailed("About EGTRAIN shows no version")
        if self.expected_version and version != self.expected_version:
            raise StepFailed(f"About EGTRAIN shows version {version}, not {self.expected_version}")
        self.press(self.button(about, "Close"))
        self.wait_closed("About EGTRAIN")
        icon_buttons = self.check_toolbar_icons()
        return (f"started from {self.package.name}, version {version}, {len(bundled)} bundled cases, "
                f"icons of the toolbar buttons {icon_buttons} drawn")

    def ask_about_updates(self, seconds: float):
        """The question about automatic update checks comes once, after the case chooser is closed, and only when the
        account has not answered it. Looks for it for some seconds and returns its window, or None."""
        try:
            return wait_until(lambda: self.window(pattern="Automatically Check"), seconds, "the question about update checks")
        except StepFailed:
            return None

    def check_toolbar_icons(self) -> str:
        toolbar = wait_until(lambda: find_first(self.main_frame(), "tool bar", max_depth=2, showing=True),
                             UI_TIMEOUT_SECONDS, "the main toolbar")
        buttons = []
        for name in TOOLBAR_ICON_BUTTONS:
            found = find_first(toolbar, "push button", name, max_depth=2, showing=True)
            if found is None or extents_of(found) is None:
                raise StepFailed(f"the toolbar button {name!r} is not there")
            buttons.append((name, extents_of(found)))
        blank = []

        def drawn():
            nonlocal blank
            blank = []  # a screenshot that fails must not leave the list of an earlier look
            blank = blank_icons(self.screenshot("toolbar", save=False), buttons)
            return not blank
        try:
            wait_until(drawn, 15, "the toolbar icons")
        except StepFailed as exc:
            if not blank:
                raise
            raise StepFailed(f"the icon area of {', '.join(blank)} has one colour, so the SVG icons are not drawn") from exc
        return ", ".join(name for name, _ in buttons)

    def step2(self) -> str:
        copy = self.copies / "downloaded" / self.bundle.name
        copy.parent.mkdir(parents=True)
        shutil.copyfile(self.bundle, copy)
        self.original_copy, self.original_mtime = copy, self.bundle.stat().st_mtime_ns
        self.choose("Open Case...", modal=True)
        dialog = self.wait_window("Open Case")
        self.set_file_name(dialog, self.bundle)
        self.press(self.button(dialog, "Open"))
        title = f"EGTRAIN - {self.case_name}"
        wait_until(lambda: name_of(self.main_frame()) == title, UI_TIMEOUT_SECONDS, f"the window title {title!r}")
        return f"opened {self.bundle.name}, the window title is {title!r}"

    def network_region(self):
        """The largest plain container of the main window, which is the network view, once its size has settled."""
        def largest():
            sizes = [extents_of(item) for item in children(self.main_frame())
                     if role_of(item) == "filler" and is_showing(item)]
            return max((size for size in sizes if size), key=lambda size: size[2] * size[3], default=None)
        region = largest()
        deadline = time.monotonic() + UI_TIMEOUT_SECONDS
        while True:
            time.sleep(0.5)
            settled = largest()
            if settled == region and settled is not None and settled[2] >= 200 and settled[3] >= 200:
                return settled
            if time.monotonic() > deadline:
                raise StepFailed("the network view has no stable place on the screen")
            region = settled

    def step3(self) -> str:
        dock = self.show_dock("Loaded Data", "Loaded Data")
        x, y, width, height = self.network_region()
        colours = count_colours(self.screenshot("network", save=False), x + 4, y + 4, width - 8, height - 8,
                                limit=MIN_NETWORK_COLOURS)
        if colours < MIN_NETWORK_COLOURS:
            raise StepFailed(f"the network view has only {colours} colours")
        advanced = self.menu_item(pattern=ADVANCED_DETAILS)
        if not has_state(advanced, Atspi.StateType.CHECKED):
            self.press(advanced)
        tree = wait_until(lambda: find_first(dock, "tree", max_depth=4), UI_TIMEOUT_SECONDS, "the Loaded Data table")
        # The tree is written again with the version rows after the switch.
        deadline = time.monotonic() + UI_TIMEOUT_SECONDS
        rows = self.loaded_rows(tree)
        while loaded_data_problems(rows, self.case_name) and time.monotonic() < deadline:
            time.sleep(POLL_SECONDS)
            rows = self.loaded_rows(tree)
        problems = loaded_data_problems(rows, self.case_name)
        if problems:
            raise StepFailed("Loaded Data lacks " + ", ".join(problems))
        self.note("Loaded Data rows: " + "; ".join(" | ".join(row) for row in rows))
        return (f"the network is drawn with at least {MIN_NETWORK_COLOURS} colours, Loaded Data lists {len(rows)} rows "
                f"for {self.case_name}")

    def loaded_rows(self, tree) -> list:
        _, cells = table_texts(tree)
        return chunk(cells, LOADED_DATA_COLUMNS)

    def loaded_value(self, label: str) -> str:
        """The Source column of the first row of Loaded Data with this category. The row Case holds the path of the open
        case."""
        dock = self.dock("Loaded Data")
        if dock is None:
            dock = self.show_dock("Loaded Data", "Loaded Data")
        for row in self.loaded_rows(find_first(dock, "tree", max_depth=4)):
            if row[0] == label:
                return row[1]
        raise StepFailed(f"Loaded Data has no row {label!r}")

    def first_track_cell(self):
        """The first cell of the table of the Infrastructure editor, which lists the track IDs."""
        dock = self.show_dock("Infrastructure", "Infrastructure")
        table = wait_until(lambda: find_first(dock, "table", "Infrastructure fields", max_depth=8, showing=True),
                           UI_TIMEOUT_SECONDS, "the table of the Infrastructure editor")
        return table, 0

    def type_into_cell(self, table, index: int, text: str) -> None:
        """Types a new value into a table cell with real keys, as a student does: a click selects the cell, F2 opens its
        editor with the old value selected, and Return ends the edit. Reads the cell back through the accessibility
        interface."""
        cell = find_all(table, "table cell", max_depth=2)[index]
        self.give_keyboard(cell)
        subprocess.run(["xdotool", "click", "1"], check=True)
        wait_until(lambda: has_state(cell, Atspi.StateType.SELECTED), UI_TIMEOUT_SECONDS, "the selected table cell")
        for keys in ("Escape", "F2", "ctrl+a"):
            subprocess.run(["xdotool", "key", "--clearmodifiers", keys], check=True)
            time.sleep(0.3)
        subprocess.run(["xdotool", "type", "--clearmodifiers", "--delay", "30", "--", text], check=True)
        subprocess.run(["xdotool", "key", "--clearmodifiers", "Return"], check=True)
        wait_until(lambda: name_of(find_all(table, "table cell", max_depth=2)[index]) == text, UI_TIMEOUT_SECONDS,
                   f"the typed value {text} in the table cell")

    def step4(self) -> str:
        table, index = self.first_track_cell()
        track = self.track_id = name_of(find_all(table, "table cell", max_depth=2)[index])
        self.type_into_cell(table, index, track + TRACK_SUFFIX)
        self.type_into_cell(table, index, track)
        settings = self.show_dock("Case Settings", "Case Settings")
        description = wait_until(lambda: find_first(settings, "text", "Description", max_depth=8, showing=True),
                                 UI_TIMEOUT_SECONDS, "the Description field of Case Settings")
        self.type_into_field(description, CASE_NOTE)
        wait_until(lambda: name_of(self.main_frame()).endswith("*"), UI_TIMEOUT_SECONDS, "the unsaved mark in the title")
        scenarios = self.show_dock("Scenarios", "Scenarios")
        library = wait_until(lambda: find_first(scenarios, "list", "Scenario library", max_depth=8, showing=True),
                             UI_TIMEOUT_SECONDS, "the scenario library")
        entry = find_first(library, "list item", max_depth=2)
        if entry is None:
            raise StepFailed("the scenario library is empty")
        if not has_state(entry, Atspi.StateType.SELECTED):
            self.press(entry)
        self.scenario_id = wait_until(
            lambda: text_of(find_first(scenarios, "text", "Scenario ID", max_depth=8, showing=True)), UI_TIMEOUT_SECONDS,
            "the scenario ID")
        scenario_description = wait_until(lambda: find_first(scenarios, "text", "Description", max_depth=8, showing=True),
                                          UI_TIMEOUT_SECONDS, "the Description field of the scenario")
        self.type_into_field(scenario_description, SCENARIO_NOTE)
        self.show_review(self.scenario_id)
        return (f"typed into the text fields Description of Case Settings and of scenario {self.scenario_id}, and into the "
                f"table cell of track {track} ({track} -> {track + TRACK_SUFFIX} -> {track}); the run review names "
                f"{self.case_name} / {self.scenario_id}")

    def show_review(self, scenario: str) -> None:
        """Opens the run review from the Simulation menu and checks that it names the case and the scenario. Closes it."""
        self.choose("Run", modal=True, menu="Simulation")
        review = self.wait_window("Run simulation")
        subtitle = f"{self.case_name} / {scenario}"
        wait_until(lambda: find_first(review, "label", subtitle, max_depth=8, showing=True), UI_TIMEOUT_SECONDS,
                   f"the run review subtitle {subtitle!r}")
        self.press(self.button(review, "Cancel"))
        self.wait_closed("Run simulation")

    def step5(self) -> str:
        self.choose("Save As...", modal=True)
        dialog = self.wait_window("Save Case As")
        proposed = text_of(find_first(dialog, "text", "File name:", max_depth=4))
        self.note(f"the dialog proposes {proposed!r}")
        self.working_copy.parent.mkdir(parents=True)
        self.set_file_name(dialog, self.working_copy)
        self.press(self.button(dialog, "Save"))
        self.wait_closed("Save Case As")
        wait_until(lambda: not name_of(self.main_frame()).endswith("*"), UI_TIMEOUT_SECONDS, "the saved title")
        wait_until(lambda: self.working_copy.is_file() and self.working_copy.stat().st_size > 0, UI_TIMEOUT_SECONDS,
                   "the working copy file")
        self.check_source_unchanged()
        if filecmp.cmp(self.working_copy, self.bundle, shallow=False):
            raise StepFailed("the working copy equals the downloaded bundle, so it holds no edit")
        tool = self.package / "usr/bin/scene_tool"
        checked = subprocess.run([str(tool), "validate", str(self.working_copy)], stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, env={"PATH": "/usr/bin:/bin"})
        if checked.returncode != 0:
            raise StepFailed("scene_tool validate rejects the working copy:\n"
                             + checked.stdout.decode(errors="replace")[-600:])
        self.saved_copy = self.copies / "working" / self.working_copy.name
        self.saved_copy.parent.mkdir(parents=True)
        shutil.copyfile(self.working_copy, self.saved_copy)
        return (f"saved {self.working_copy.name} ({self.working_copy.stat().st_size} bytes), proposed as {proposed!r}; "
                "the downloaded bundle is unchanged")

    def check_source_unchanged(self) -> None:
        """The downloaded bundle equals its copy from before step 2 byte for byte and has its old modification time."""
        if not filecmp.cmp(self.bundle, self.original_copy, shallow=False):
            raise StepFailed("the downloaded bundle differs from its copy")
        if self.bundle.stat().st_mtime_ns != self.original_mtime:
            raise StepFailed("the downloaded bundle has a new modification time")

    def step6(self) -> str:
        source = self.loaded_value(LOADED_DATA_CASE)
        if source != str(self.working_copy):
            raise StepFailed(f"the open case is {source}, not the working copy")
        self.choose("Run", modal=True, menu="Simulation")
        review = self.wait_window("Run simulation")
        self.press(self.button(review, "Run simulation"))
        self.wait_closed("Run simulation")
        summary = wait_until(lambda: find_first(self.main_frame(), "label", pattern=RUN_COMPLETED, max_depth=10),
                             RUN_TIMEOUT_SECONDS, "the finished run")
        return f"ran {name_of(summary)}"

    def step7(self) -> str:
        opened = []
        self.choose("Timetable", modal=True)
        timetable = self.wait_window(pattern=r"^Timetable")
        table = wait_until(lambda: find_first(timetable, "table", showing=True), UI_TIMEOUT_SECONDS,
                           "the timetable table")
        headers, cells = table_texts(table)
        problems = timetable_problems(headers, cells)
        if problems:
            raise StepFailed("the timetable table lacks " + ", ".join(problems))
        opened.append(name_of(timetable))
        self.screenshot("timetable")
        views = (("Delays", r"^Delays", "delay"),
                 ("Speed / time", r"^Speed / time", "speed"))
        for menu, pattern, label in views:
            self.choose(menu, modal=True)
            opened.append(name_of(self.wait_window(pattern=pattern)))
            self.screenshot(label)
        self.choose("Blocking time...", modal=True)
        # The choice of blocks and the chart are both titled Blocking time; only the choice has an OK button.
        scope = self.wait_window("Blocking time")
        self.press(self.button(scope, "OK"))

        def chart():
            window = self.window("Blocking time")
            return window if window is not None and find_first(window, "push button", "OK", max_depth=10) is None else None
        opened.append(name_of(wait_until(chart, UI_TIMEOUT_SECONDS, "the Blocking time chart")))
        self.screenshot("blocking-time")
        return ("opened " + "; ".join(opened)
                + f"; the timetable has {len(headers)} columns and {len(cells) // len(headers)} rows")

    def export(self, button_name: str, dialog_name: str, path: Path) -> None:
        def shown():
            frame = find_first(self.main_frame(), "frame", pattern=r"^Run Results", max_depth=1)
            return frame if frame is not None and is_showing(frame) else None
        if shown() is None:
            self.press(self.menu_item(pattern=r"^Run Results"))
        results = wait_until(shown, UI_TIMEOUT_SECONDS, "the Run Results dock")
        self.press(self.button(results, button_name), modal=True)
        dialog = self.wait_window(dialog_name)
        self.set_file_name(dialog, path)
        self.press(self.button(dialog, "Save"))
        self.wait_closed(dialog_name)
        wait_until(lambda: path.is_file() and path.stat().st_size > 0, UI_TIMEOUT_SECONDS, f"the file {path.name}")

    def step8(self) -> str:
        self.exports.mkdir(parents=True)
        csv_path, png_path = self.exports / "run_summary.csv", self.exports / "run_summary.png"
        self.export("Export CSV...", "Export Data", csv_path)
        problem = csv_problem(csv_path.read_text(encoding="utf-8"))
        if problem:
            raise StepFailed(f"{csv_path.name}: {problem}")
        self.export("Export PNG...", "Export Table", png_path)
        problem = png_problem(png_path.read_bytes())
        if problem:
            raise StepFailed(f"{png_path.name}: {problem}")
        return (f"exported {csv_path.name} ({csv_path.stat().st_size} bytes) and {png_path.name} "
                f"({png_path.stat().st_size} bytes) to {self.exports}")

    def step9(self) -> str:
        self.choose("Quit", modal=True)
        try:
            self.process.wait(timeout=QUIT_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired as exc:
            alert = self.window(pattern=r"^Unsaved")
            raise StepFailed("the application did not quit" + (f" and asks {name_of(alert)!r}" if alert else "")) from exc
        if self.process.returncode != 0:
            raise StepFailed(f"the application quit with the code {self.process.returncode}")
        self.process = None
        if not filecmp.cmp(self.working_copy, self.saved_copy, shallow=False):
            raise StepFailed("the working copy changed while the application ran and quit")
        self.start_application()
        self.app = self.application()
        chooser = wait_until(lambda: self.window("Open a Case"), STARTUP_TIMEOUT_SECONDS,
                             "the case chooser of the second start")
        recent = [name_of(item) for item in find_all(chooser, "list item", max_depth=6)]
        self.note("the case chooser of the second start lists " + ", ".join(recent))
        self.press(find_first(chooser, "push button", "Continue", max_depth=4) or self.button(chooser, "Cancel"))
        self.wait_closed("Open a Case")
        if self.ask_about_updates(5) is not None:
            raise StepFailed("the question about update checks came again")
        self.choose("Open Case...", modal=True)
        dialog = self.wait_window("Open Case")
        self.set_file_name(dialog, self.working_copy)
        self.press(self.button(dialog, "Open"))
        wait_until(lambda: name_of(self.main_frame()) == f"EGTRAIN - {self.case_name}", UI_TIMEOUT_SECONDS,
                   "the reopened case")
        if self.loaded_value(LOADED_DATA_CASE) != str(self.working_copy):
            raise StepFailed("the reopened case is not the working copy")
        settings = self.show_dock("Case Settings", "Case Settings")
        description = wait_until(lambda: find_first(settings, "text", "Description", max_depth=8, showing=True),
                                 UI_TIMEOUT_SECONDS, "the Description field of Case Settings")
        if text_of(description) != CASE_NOTE:
            raise StepFailed(f"the case description is {text_of(description)!r}, not the edit {CASE_NOTE!r}")
        scenarios = self.show_dock("Scenarios", "Scenarios")
        scenario_field = wait_until(lambda: find_first(scenarios, "text", "Description", max_depth=8, showing=True),
                                    UI_TIMEOUT_SECONDS, "the Description field of the scenario")
        if text_of(scenario_field) != SCENARIO_NOTE:
            raise StepFailed(f"the scenario description is {text_of(scenario_field)!r}, not the edit {SCENARIO_NOTE!r}")
        table, index = self.first_track_cell()
        if name_of(find_all(table, "table cell", max_depth=2)[index]) != self.track_id:
            raise StepFailed(f"the first track is not {self.track_id} in the reopened copy")
        self.check_source_unchanged()
        return (f"quit with code 0, started again and reopened {self.working_copy.name} with the case and scenario edits "
                f"and track {self.track_id} as before; the downloaded bundle equals its copy and has its old modification time")

    # The run

    def run(self) -> int:
        for path in (self.home, self.runtime, self.evidence):
            path.mkdir(parents=True, exist_ok=True)
        os.chmod(self.runtime, 0o700)
        self.log_file = (self.evidence / "linux-rehearsal.log").open("w", encoding="utf-8")
        steps = {1: self.step1, 2: self.step2, 3: self.step3, 4: self.step4, 5: self.step5, 6: self.step6, 7: self.step7,
                 8: self.step8, 9: self.step9}
        failed = None
        try:
            for number, title in STEPS:
                if number > self.last_step:
                    break
                self.note(f"step {number}: {title}")
                try:
                    detail = steps[number]()
                    self.screenshot(f"step-{number}")
                except StepFailed as exc:
                    failed = (number, title, str(exc))
                    break
                except Exception as exc:  # a step fails on anything that the script did not foresee
                    failed = (number, title, f"{type(exc).__name__}: {exc}")
                    traceback.print_exc(file=self.log_file)
                    break
                print(format_step_line(number, True, detail), flush=True)
                self.note(format_step_line(number, True, detail), echo=False)
        finally:
            if failed:
                self.write_failure(failed)
            self.stop_application()
            if self.app_log:
                self.app_log.close()
        if failed:
            number, title, detail = failed
            print(format_step_line(number, False, detail), flush=True)
            self.note(format_step_line(number, False, detail), echo=False)
            print(f"Linux rehearsal failed at step {number}: {title}", flush=True)
            self.log_file.close()
            return 1
        print(f"Linux rehearsal passed steps 1 to {min(self.last_step, len(STEPS))}", flush=True)
        self.log_file.close()
        return 0

    def write_failure(self, failed) -> None:
        number, title, detail = failed
        try:
            self.screenshot(f"step-{number}-failed")
        except (StepFailed, OSError, ValueError) as exc:
            self.note(f"no screenshot of the failure: {exc}")
        with (self.evidence / "failure-tree.txt").open("w", encoding="utf-8") as out:
            out.write(f"step {number}: {title}\n{detail}\n\n")
            try:
                dump_tree(self.app, out)
            except (AttributeError, GLib.Error) as exc:
                out.write(f"no accessibility tree: {exc}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", type=Path, help="the extracted AppImage, with AppRun")
    parser.add_argument("bundle", type=Path, help="the downloaded .egscene file")
    parser.add_argument("--work", type=Path, default=Path("linux-rehearsal"),
                        help="a new directory for the files of the run")
    parser.add_argument("--evidence", type=Path, help="the directory for screenshots and logs, default: evidence in --work")
    parser.add_argument("--expected-version", help="the version that About EGTRAIN must show")
    parser.add_argument("--last-step", type=int, default=len(STEPS), help="stop after this step")
    args = parser.parse_args()

    package, bundle, work = args.package.resolve(), args.bundle.resolve(), args.work.resolve()
    if not (package / "AppRun").is_file():
        environment_error(f"{package} has no AppRun: pass the extracted AppImage")
    if not bundle.is_file():
        environment_error(f"{bundle} is not a file")
    for variable in ("DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
        if not os.environ.get(variable):
            environment_error(f"{variable} is not set: run under xvfb-run and dbus-run-session")
    for tool in ("xdotool", "import", "convert"):
        if shutil.which(tool) is None:
            environment_error(f"{tool} is not installed")
    if work.exists() and any(work.iterdir()):
        environment_error(f"{work} is not empty")
    if not 1 <= args.last_step <= len(STEPS):
        environment_error(f"--last-step is a number from 1 to {len(STEPS)}")
    load_atspi()
    connection = start_accessibility_bus()
    try:
        evidence = (args.evidence or work / "evidence").resolve()
        rehearsal = Rehearsal(package, bundle, work, evidence, args.expected_version, args.last_step)
        return rehearsal.run()
    finally:
        connection.close()


if __name__ == "__main__":
    sys.exit(main())
