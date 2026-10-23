#!/usr/bin/env python3
import contextlib
import io
import json
import re
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import linux_rehearsal


def ppm(width, height, pixel_at, header=b"P6\n"):
    data = bytearray()
    for row in range(height):
        for column in range(width):
            data.extend(pixel_at(column, row))
    return header + f"{width} {height}\n255\n".encode() + bytes(data)


WHITE, BLACK, RED = (255, 255, 255), (0, 0, 0), (200, 20, 20)


def flat(colour):
    return lambda column, row: colour


class StepTableTests(unittest.TestCase):
    def test_steps_are_the_nine_steps_of_the_checklist_in_order(self):
        self.assertEqual([number for number, _ in linux_rehearsal.STEPS], list(range(1, 10)))
        titles = [title for _, title in linux_rehearsal.STEPS]
        self.assertTrue(all(titles))
        self.assertEqual(len(set(titles)), len(titles))

    def test_every_step_has_a_method(self):
        for number, _ in linux_rehearsal.STEPS:
            self.assertTrue(callable(getattr(linux_rehearsal.Rehearsal, f"step{number}")), number)

    def test_step_line_is_one_line_with_the_result_and_the_detail(self):
        self.assertEqual(linux_rehearsal.format_step_line(3, True, "drawn"), "STEP 3 PASS drawn")
        self.assertEqual(linux_rehearsal.format_step_line(4, False, "timed out\nafter  60 s"),
                         "STEP 4 FAIL timed out after 60 s")
        self.assertEqual(linux_rehearsal.format_step_line(1, True, ""), "STEP 1 PASS")


class ImageTests(unittest.TestCase):
    def test_ppm_is_read_with_comments_in_the_header(self):
        image = linux_rehearsal.parse_ppm(ppm(3, 2, flat(RED), header=b"P6\n# written by a tool\n"))
        self.assertEqual((image.width, image.height, len(image.pixels)), (3, 2, 18))
        self.assertEqual(image.pixels[:3], bytes(RED))

    def test_ppm_with_sixteen_bits_or_missing_data_is_refused(self):
        for data in (b"P6\n2 2\n65535\n" + bytes(24), b"P6\n2 2\n255\n" + bytes(5), b"P5\n2 2\n255\n" + bytes(4), b"P6\n2"):
            with self.assertRaises(ValueError, msg=data):
                linux_rehearsal.parse_ppm(data)

    def test_colours_are_counted_in_the_region_only_and_the_region_is_clipped(self):
        image = linux_rehearsal.parse_ppm(ppm(10, 10, lambda column, row: RED if column >= 6 else WHITE))
        self.assertEqual(linux_rehearsal.count_colours(image, 0, 0, 5, 10), 1)
        self.assertEqual(linux_rehearsal.count_colours(image, 4, 0, 4, 10), 2)
        self.assertEqual(linux_rehearsal.count_colours(image, -5, -5, 100, 100), 2)
        self.assertEqual(linux_rehearsal.count_colours(image, 20, 20, 5, 5), 0)
        self.assertGreaterEqual(linux_rehearsal.count_colours(image, 0, 0, 10, 10, limit=2), 2)
        self.assertEqual(linux_rehearsal.count_colours(image, 0, 0, 5, 10, limit=2), 1)

    def test_a_flat_icon_button_is_found_and_a_drawn_one_is_not(self):
        def pixel(column, row):
            return BLACK if (column == 108 and row == 10) else WHITE
        image = linux_rehearsal.parse_ppm(ppm(200, 40, pixel))
        buttons = [("Zoom In", (100, 0, 34, 32)), ("Zoom Out", (140, 0, 34, 32))]
        self.assertEqual(linux_rehearsal.blank_icons(image, buttons), ["Zoom Out"])


class ReadingTests(unittest.TestCase):
    def test_version_is_read_from_the_text_of_the_about_box(self):
        self.assertEqual(linux_rehearsal.version_of("EGTRAIN Version 1.2.3\nMicroscopic railway simulation"), "1.2.3")
        self.assertIsNone(linux_rehearsal.version_of("EGTRAIN"))

    def test_chunk_drops_an_incomplete_row(self):
        self.assertEqual(linux_rehearsal.chunk(list("abcdefg"), 3), [("a", "b", "c"), ("d", "e", "f")])

    def rows(self):
        """The rows of Loaded Data for Paimpol with advanced details, as the dock shows them with the case root expanded."""
        return [("Case", "/x/Paimpol.egscene", "", "Warning"), ("Name", "Paimpol", "", "OK"),
                ("Description", "Signalling: one network-wide area, level 0.", "", "OK"), ("Format version", "1", "1", "OK"),
                ("Saved with", "1.0.0", "", "OK"), ("Bundle version", "1", "1", "OK"),
                ("Source files discovered", "", "9", "OK"), ("Case metadata", "scene.json", "", "Warning"),
                ("Infrastructure", "infrastructure.json", "946", "Warning"), ("Stations", "stations.json", "10", "OK"),
                ("Timetable", "services.json", "2", "OK"), ("Rolling stock", "rolling_stock.json", "2", "OK"),
                ("Signalling", "signalling.json", "20", "OK"), ("Scenarios", "scenarios.json", "1", "OK"),
                ("Passengers", "passengers.json", "188", "Warning"), ("Run", "", "", "Not run")]

    def test_complete_loaded_data_has_no_problem(self):
        self.assertEqual(linux_rehearsal.loaded_data_problems(self.rows(), "Paimpol"), [])

    def test_loaded_data_reports_what_is_missing(self):
        rows = [row for row in self.rows() if row[0] not in ("Bundle version", "Run")]
        rows = [("Scenarios", "scenarios.json", "0", "OK") if row[0] == "Scenarios" else row for row in rows]
        problems = linux_rehearsal.loaded_data_problems(rows, "Other")
        self.assertEqual(problems, ["the name of the case (Other)", "the row Bundle version", "a scenario", "the row Run"])

    def test_loaded_data_without_advanced_details_lacks_the_versions(self):
        rows = [row for row in self.rows() if row[0] not in linux_rehearsal.LOADED_DATA_VERSIONS + ("Source files discovered",)]
        self.assertEqual(linux_rehearsal.loaded_data_problems(rows, "Paimpol"),
                         ["the row Format version", "the row Saved with", "the row Bundle version"])

    def test_the_rows_that_the_rehearsal_reads_are_written_by_the_panel(self):
        panel = (Path(__file__).resolve().parents[2] / "EGTRAIN/QEGTRAIN/editors/ValidationPanel.cpp").read_text(encoding="utf-8")
        for label in linux_rehearsal.LOADED_DATA_VERSIONS + (linux_rehearsal.LOADED_DATA_CASE, linux_rehearsal.LOADED_DATA_RUN,
                                                             "Name"):
            self.assertIn(f'"{label}"', panel, f"ValidationPanel.cpp writes no row {label!r}")

    def test_the_advanced_details_entry_is_found_in_any_capitals(self):
        window = (Path(__file__).resolve().parents[2] / "EGTRAIN/QEGTRAIN/app/MainWindow.cpp").read_text(encoding="utf-8")
        entry = re.search(r'm_advancedDetailsAction = new QAction\(QStringLiteral\("([^"]+)"\)', window)
        self.assertIsNotNone(entry, "MainWindow.cpp creates no advanced details action")
        for name in (entry.group(1), "Advanced / Developer Details", "Advanced Details"):
            self.assertRegex(name, linux_rehearsal.ADVANCED_DETAILS)
        self.assertNotRegex("Loaded Data", linux_rehearsal.ADVANCED_DETAILS)

    def test_the_finished_run_is_found_with_and_without_advanced_details(self):
        self.assertRegex("Run 2\nStatus: Completed", linux_rehearsal.RUN_COMPLETED)
        self.assertRegex("Run 2\nStatus: Completed\nIncidents acted on 0 of 2 trains\nDestination terminations: 0",
                         linux_rehearsal.RUN_COMPLETED)
        self.assertNotRegex("Run 2\nStatus: Running", linux_rehearsal.RUN_COMPLETED)

    def test_loaded_data_without_a_count_or_status_is_reported(self):
        rows = [("Stations", "stations.json", "", "OK") if row[0] == "Stations" else row for row in self.rows()]
        self.assertEqual(linux_rehearsal.loaded_data_problems(rows, "Paimpol"), ["the count and the status of Stations"])

    def test_timetable_needs_planned_and_simulated_values(self):
        headers = ["Train", "Planned arrival", "Planned departure", "Simulated arrival", "Simulated departure"]
        cells = ["T1", "-", "07:38:05", "07:36:56", "07:38:05", "T1", "07:41:03", "07:41:33", "07:40:42", "07:41:33"]
        self.assertEqual(linux_rehearsal.timetable_problems(headers, cells), [])
        self.assertEqual(linux_rehearsal.timetable_problems(headers[:3] + headers[4:], cells), ["the column Simulated arrival"])
        self.assertEqual(linux_rehearsal.timetable_problems(headers, ["T1", "-", "-", "-", "-"]),
                         ["a stop with a planned and a simulated departure time"])

    def test_csv_text(self):
        self.assertIsNone(linux_rehearsal.csv_problem("Train,Start\nT1,10\n"))
        self.assertEqual(linux_rehearsal.csv_problem(""), "it is empty")
        self.assertEqual(linux_rehearsal.csv_problem("Train Start\nT1 10\n"), "the first line has no comma")
        self.assertEqual(linux_rehearsal.csv_problem("Train,Start\n\n"), "it has a header line and no data line")

    def png(self, width=1920, height=353, size=5000):
        header = (linux_rehearsal.PNG_SIGNATURE + (13).to_bytes(4, "big") + b"IHDR" + width.to_bytes(4, "big")
                  + height.to_bytes(4, "big"))
        return header + bytes(size - len(header))

    def test_png_bytes(self):
        self.assertIsNone(linux_rehearsal.png_problem(self.png()))
        self.assertEqual(linux_rehearsal.png_problem(b"GIF89a" + bytes(2000)), "it does not start with the PNG signature")
        self.assertEqual(linux_rehearsal.png_problem(linux_rehearsal.PNG_SIGNATURE + bytes(2000)),
                         "it has no IHDR chunk after the signature")
        self.assertEqual(linux_rehearsal.png_problem(self.png(width=0)), "the image has no size")
        self.assertEqual(linux_rehearsal.png_problem(self.png(size=900)), "the file has only 900 bytes")

    def test_case_name_is_read_from_the_bundle(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = Path(temp) / "Paimpol.egscene"
            with zipfile.ZipFile(bundle, "w") as archive:
                archive.writestr("scene.json", json.dumps({"name": "Paimpol", "version": 1}))
            self.assertEqual(linux_rehearsal.bundle_case_name(bundle), "Paimpol")


class EnvironmentTests(unittest.TestCase):
    def test_application_gets_a_new_home_and_only_the_accessibility_switches_of_qt(self):
        home, runtime = Path("/tmp/home"), Path("/tmp/runtime")
        env = linux_rehearsal.application_environment(home, runtime)
        self.assertEqual(env["HOME"], str(home))
        self.assertEqual(env["XDG_RUNTIME_DIR"], str(runtime))
        self.assertEqual((env["QT_ACCESSIBILITY"], env["QT_LINUX_ACCESSIBILITY_ALWAYS_ON"]), ("1", "1"))
        self.assertEqual([name for name in env if name.startswith(("QT_", "QEGTRAIN_"))],
                         ["QT_ACCESSIBILITY", "QT_LINUX_ACCESSIBILITY_ALWAYS_ON"])
        self.assertFalse([name for name in env if name.startswith(("LD_", "QML", "VCPKG", "CMAKE_"))])
        self.assertEqual(env["PATH"], "/usr/bin:/bin")

    def test_environment_of_the_script_does_not_reach_the_application(self):
        original = dict(linux_rehearsal.os.environ)
        try:
            linux_rehearsal.os.environ.update({"QEGTRAIN_AUTOSTART": "1", "LD_LIBRARY_PATH": "/opt/qt/lib",
                                               "QT_PLUGIN_PATH": "/x", "DISPLAY": ":7",
                                               "DBUS_SESSION_BUS_ADDRESS": "unix:path=/bus"})
            env = linux_rehearsal.application_environment(Path("/tmp/home"), Path("/tmp/runtime"))
        finally:
            linux_rehearsal.os.environ.clear()
            linux_rehearsal.os.environ.update(original)
        self.assertEqual((env["DISPLAY"], env["DBUS_SESSION_BUS_ADDRESS"]), (":7", "unix:path=/bus"))
        self.assertNotIn("QEGTRAIN_AUTOSTART", env)
        self.assertNotIn("LD_LIBRARY_PATH", env)
        self.assertNotIn("QT_PLUGIN_PATH", env)


class FakeAccessible:
    def __init__(self, role, name="", showing=True, *kids):
        self.role, self.name, self.showing, self.kids = role, name, showing, list(kids)

    def get_role_name(self):
        return self.role

    def get_name(self):
        return self.name

    def get_child_count(self):
        return len(self.kids)

    def get_child_at_index(self, index):
        return self.kids[index]

    def get_state_set(self):
        return SimpleNamespace(contains=lambda state: self.showing)


class TreeSearchTests(unittest.TestCase):
    def setUp(self):
        self.saved = linux_rehearsal.Atspi, linux_rehearsal.GLib
        linux_rehearsal.Atspi = SimpleNamespace(StateType=SimpleNamespace(SHOWING="showing"))
        linux_rehearsal.GLib = SimpleNamespace(Error=RuntimeError)
        hidden = FakeAccessible("push button", "OK", False)
        shown = FakeAccessible("push button", "OK", True)
        deep = FakeAccessible("push button", "Deep", True)
        self.tree = FakeAccessible("frame", "Main", True, hidden, FakeAccessible("panel", "", True, shown),
                                   FakeAccessible("panel", "", True, FakeAccessible("filler", "", True, deep)))
        self.shown, self.hidden, self.deep = shown, hidden, deep

    def tearDown(self):
        linux_rehearsal.Atspi, linux_rehearsal.GLib = self.saved

    def test_find_first_takes_the_first_match_or_the_first_showing_match(self):
        self.assertIs(linux_rehearsal.find_first(self.tree, "push button", "OK"), self.hidden)
        self.assertIs(linux_rehearsal.find_first(self.tree, "push button", "OK", showing=True), self.shown)

    def test_search_depth_is_limited(self):
        self.assertIsNone(linux_rehearsal.find_first(self.tree, "push button", "Deep", max_depth=2))
        self.assertIs(linux_rehearsal.find_first(self.tree, "push button", "Deep", max_depth=3), self.deep)

    def test_pattern_and_missing_match(self):
        self.assertIs(linux_rehearsal.find_first(self.tree, pattern="^Dee"), self.deep)
        self.assertEqual(linux_rehearsal.find_all(self.tree, "push button"), [self.hidden, self.shown, self.deep])
        self.assertIsNone(linux_rehearsal.find_first(self.tree, "dialog"))


def make_rehearsal(temp):
    temp = Path(temp)
    bundle = temp / "Paimpol.egscene"
    with zipfile.ZipFile(bundle, "w") as archive:
        archive.writestr("scene.json", json.dumps({"name": "Paimpol", "version": 1}))
    return linux_rehearsal.Rehearsal(temp / "package", bundle, temp / "work", temp / "evidence", None, 1)


def unreadable_screen(label, save=True):
    raise linux_rehearsal.StepFailed("import could not read the screen")


class UnreadableScreenTests(unittest.TestCase):
    def test_a_screenshot_that_fails_after_a_step_fails_that_step(self):
        with tempfile.TemporaryDirectory() as temp:
            rehearsal = make_rehearsal(temp)
            rehearsal.step1 = lambda: "started"
            rehearsal.stop_application = lambda: None
            rehearsal.write_failure = lambda failed: None
            rehearsal.screenshot = unreadable_screen
            with contextlib.redirect_stdout(io.StringIO()) as printed:
                self.assertEqual(rehearsal.run(), 1)
        lines = printed.getvalue().splitlines()
        self.assertIn("STEP 1 FAIL import could not read the screen", lines)
        self.assertNotIn("STEP 1 PASS started", lines)

    def test_an_unreadable_screen_is_not_reported_as_a_blank_icon(self):
        saved = linux_rehearsal.Atspi, linux_rehearsal.GLib, linux_rehearsal.extents_of
        linux_rehearsal.Atspi = SimpleNamespace(StateType=SimpleNamespace(SHOWING="showing"))
        linux_rehearsal.GLib = SimpleNamespace(Error=RuntimeError)
        linux_rehearsal.extents_of = lambda accessible: (10, 10, 30, 30)
        try:
            buttons = [FakeAccessible("push button", name, True) for name in linux_rehearsal.TOOLBAR_ICON_BUTTONS]
            frame = FakeAccessible("frame", "EGTRAIN", True, FakeAccessible("tool bar", "", True, *buttons))
            with tempfile.TemporaryDirectory() as temp:
                rehearsal = make_rehearsal(temp)
                rehearsal.main_frame = lambda: frame
                rehearsal.screenshot = unreadable_screen
                with self.assertRaises(linux_rehearsal.StepFailed) as raised:
                    rehearsal.check_toolbar_icons()
        finally:
            linux_rehearsal.Atspi, linux_rehearsal.GLib, linux_rehearsal.extents_of = saved
        self.assertEqual(str(raised.exception), "import could not read the screen")


class WaitTests(unittest.TestCase):
    def test_wait_returns_the_first_value_that_is_true(self):
        values = iter([None, False, "found"])
        self.assertEqual(linux_rehearsal.wait_until(lambda: next(values), 5, "a value"), "found")

    def test_wait_names_what_it_waited_for(self):
        with self.assertRaises(linux_rehearsal.StepFailed) as raised:
            linux_rehearsal.wait_until(lambda: None, 0, "the main toolbar")
        self.assertEqual(str(raised.exception), "timed out after 0 s waiting for the main toolbar")


if __name__ == "__main__":
    unittest.main()
