#!/usr/bin/env python3
import contextlib
import io
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

import measure_peak_rss


class TestMacosTimeParser(unittest.TestCase):
    def test_parses_realistic_output(self):
        output = """        0.02 real         0.00 user         0.00 sys
           1196032  maximum resident set size
                 0  average shared memory size
            884928  peak memory footprint
"""
        self.assertEqual(measure_peak_rss.parse_macos_time_peak_rss(output), 1196032)

    def test_parses_whitespace_and_large_integer(self):
        output = "\t  9876543210   maximum resident set size   \n"
        self.assertEqual(measure_peak_rss.parse_macos_time_peak_rss(output), 9876543210)

    def test_rejects_missing_duplicate_or_malformed_rss(self):
        invalid = (
            "884928 peak memory footprint\n",
            "10 maximum resident set size\n20 maximum resident set size\n",
            "0 maximum resident set size\n",
            "-1 maximum resident set size\n",
            "1.5 maximum resident set size\n",
            "1,024 maximum resident set size\n",
            "many maximum resident set size\n",
        )
        for output in invalid:
            with self.subTest(output=output):
                with self.assertRaises(ValueError):
                    measure_peak_rss.parse_macos_time_peak_rss(output)


class TestMeasurementContract(unittest.TestCase):
    def test_commands_use_native_time_and_canonical_flags(self):
        app = Path("/tmp/build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN")
        scenes = Path("/checkout/EGTRAIN/QEGTRAIN/Scenes")
        for case_name in measure_peak_rss.CASES:
            self.assertEqual(
                measure_peak_rss.case_command(app, scenes / case_name),
                [
                    str(measure_peak_rss.TIME_TOOL),
                    "-l",
                    str(app),
                    "--scene",
                    str(scenes / case_name),
                    "-g",
                    "0",
                    "-TSM",
                    "0",
                    "-RC",
                    "0",
                ],
            )

    def test_generated_output_must_be_in_build_or_outside_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            allowed = root / "build/memory/issue-62"
            self.assertEqual(
                measure_peak_rss.validate_output_dir(allowed, root), allowed
            )
            outside = root.parent / "external-memory-results"
            self.assertEqual(
                measure_peak_rss.validate_output_dir(outside, root), outside.resolve()
            )
            for rejected in (root, root / "tools/results", root / "EGTRAIN/QEGTRAIN/Output"):
                with self.subTest(rejected=rejected):
                    with self.assertRaises(ValueError):
                        measure_peak_rss.validate_output_dir(rejected, root)
            output_link = root / "build-link"
            output_link.symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(
                measure_peak_rss.MeasurementError, "output path must not be a symlink"
            ):
                measure_peak_rss.validate_output_dir(output_link, root)

    def test_build_type_comes_from_cache_and_rejects_conflicting_explicit_value(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN"
            app.parent.mkdir(parents=True)
            app.touch()
            (root / "build/CMakeCache.txt").write_text(
                "OTHER:STRING=x\nCMAKE_BUILD_TYPE:STRING=Release\n", encoding="utf-8"
            )
            self.assertEqual(measure_peak_rss.cmake_build_type(app, None), "Release")
            self.assertEqual(
                measure_peak_rss.cmake_build_type(app, "Release"), "Release"
            )
            with self.assertRaisesRegex(
                measure_peak_rss.MeasurementError,
                "conflicts.*CMAKE_BUILD_TYPE 'Release'",
            ):
                measure_peak_rss.cmake_build_type(app, "Debug")

    def test_external_app_requires_explicit_build_type(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "build").mkdir()
            (root / "build/CMakeCache.txt").write_text(
                "CMAKE_BUILD_TYPE:STRING=Release\n", encoding="utf-8"
            )
            external_app = root / "external/QEGTRAIN"
            external_app.parent.mkdir()
            external_app.touch()
            (external_app.parent / "CMakeCache.txt").write_text(
                "CMAKE_BUILD_TYPE:STRING=\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(
                measure_peak_rss.MeasurementError, "external application"
            ):
                measure_peak_rss.cmake_build_type(external_app, None)
            self.assertEqual(
                measure_peak_rss.cmake_build_type(external_app, "RelWithDebInfo"),
                "RelWithDebInfo",
            )

    def test_owned_output_directories_reject_symlinks(self):
        for relative in ("logs", "runtime"):
            with self.subTest(relative=relative), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                output = root / "output"
                output.mkdir()
                target = root / "target"
                target.mkdir()
                (output / relative).symlink_to(target, target_is_directory=True)
                with self.assertRaisesRegex(
                    measure_peak_rss.MeasurementError, "must not be a symlink"
                ):
                    measure_peak_rss.prepare_output_directory(output)
                self.assertEqual(list(target.iterdir()), [])

    def test_case_runtime_destination_components_reject_symlinks(self):
        components = (
            "runtime/Copenhagen",
            "runtime/Copenhagen/Output",
            "runtime/Copenhagen/Output/Copenhagen",
        )
        for relative in components:
            with self.subTest(relative=relative), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                output = root / "output"
                measure_peak_rss.prepare_output_directory(output)
                target = root / "target"
                target.mkdir()
                candidate = output / relative
                candidate.parent.mkdir(parents=True, exist_ok=True)
                candidate.symlink_to(target, target_is_directory=True)
                with self.assertRaisesRegex(
                    measure_peak_rss.MeasurementError, "must not be a symlink"
                ):
                    measure_peak_rss.prepare_case_output("Copenhagen", output)
                self.assertEqual(list(target.iterdir()), [])

    def test_log_leaves_reject_symlinks_without_altering_targets(self):
        for suffix in ("stdout", "stderr"):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                output = root / "output"
                measure_peak_rss.prepare_output_directory(output)
                target = root / "target.log"
                target.write_text("sentinel", encoding="utf-8")
                log = output / "logs" / f"Copenhagen.{suffix}.log"
                log.symlink_to(target)
                with self.assertRaisesRegex(
                    measure_peak_rss.MeasurementError, "must not be a symlink"
                ):
                    measure_peak_rss.prepare_case_output("Copenhagen", output)
                self.assertEqual(target.read_text(encoding="utf-8"), "sentinel")

    def test_result_and_temporary_leaves_reject_symlinks(self):
        for relative in ("peak-rss.json", "peak-rss.json.tmp"):
            with self.subTest(relative=relative), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                output = root / "output"
                output.mkdir()
                target = root / "target.json"
                target.write_text("sentinel", encoding="utf-8")
                (output / relative).symlink_to(target)
                with self.assertRaisesRegex(
                    measure_peak_rss.MeasurementError, "must not be a symlink"
                ):
                    if relative.endswith(".tmp"):
                        measure_peak_rss.write_json_atomic(
                            output / "peak-rss.json", {"value": 1}
                        )
                    else:
                        measure_peak_rss.prepare_output_directory(output)
                self.assertEqual(target.read_text(encoding="utf-8"), "sentinel")

    def test_normal_existing_output_directory_supports_reruns(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "output"
            for value in (1, 2):
                result = measure_peak_rss.prepare_output_directory(output)
                _, _, stdout_log, stderr_log = measure_peak_rss.prepare_case_output(
                    "Copenhagen", output
                )
                with measure_peak_rss.open_output_log(
                    stdout_log, output
                ) as stdout_file, measure_peak_rss.open_output_log(
                    stderr_log, output
                ) as stderr_file:
                    stdout_file.write(b"stdout")
                    stderr_file.write(b"stderr")
                measure_peak_rss.write_json_atomic(result, {"value": value})
            self.assertIn('"value": 2', result.read_text(encoding="utf-8"))

    def test_host_errors_precede_build_metadata_resolution(self):
        arguments = [
            "--app",
            "/external/QEGTRAIN",
            "--output-dir",
            "/tmp/measurement",
        ]
        for platform_name, time_tool, expected in (
            ("linux", Path("/usr/bin/time"), "supported only on macOS"),
            ("darwin", Path("/definitely/missing/native-time"), "tool is missing"),
        ):
            with self.subTest(expected=expected), mock.patch.object(
                measure_peak_rss.sys, "platform", platform_name
            ), mock.patch.object(measure_peak_rss, "TIME_TOOL", time_tool):
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr):
                    self.assertEqual(measure_peak_rss.main(arguments), 1)
                self.assertIn(expected, stderr.getvalue())
                self.assertNotIn("build type", stderr.getvalue())

    @unittest.skipUnless(hasattr(os, "killpg"), "requires POSIX process groups")
    def test_timeout_kills_spawned_child_process(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            child_pid_file = temporary / "child.pid"
            program = """
import subprocess
import sys
import time
from pathlib import Path

child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
Path(sys.argv[1]).write_text(str(child.pid), encoding="utf-8")
time.sleep(60)
"""
            with open(os.devnull, "wb") as devnull:
                with self.assertRaises(subprocess.TimeoutExpired):
                    measure_peak_rss.run_process_group(
                        [sys.executable, "-c", program, str(child_pid_file)],
                        cwd=temporary,
                        env=os.environ.copy(),
                        stdout=devnull,
                        stderr=devnull,
                        timeout=1.0,
                    )
            child_pid = int(child_pid_file.read_text(encoding="utf-8"))
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                try:
                    os.kill(child_pid, 0)
                except ProcessLookupError:
                    break
                time.sleep(0.05)
            else:
                self.fail(f"spawned child {child_pid} survived process-group timeout")

    def test_run_record_contains_required_schema_fields(self):
        output = Path("/tmp/measurement")
        command = ["/usr/bin/time", "-l", "/tmp/QEGTRAIN"]
        record = measure_peak_rss.make_run_record(
            case_name="Copenhagen",
            run_order=1,
            command=command,
            working_directory=Path("/checkout/EGTRAIN/QEGTRAIN"),
            runtime_output=output / "runtime/Copenhagen",
            stdout_log=output / "logs/Copenhagen.stdout.log",
            stderr_log=output / "logs/Copenhagen.stderr.log",
            output_dir=output,
            peak_rss_bytes=1048576,
            elapsed_seconds=12.3456789,
        )
        self.assertEqual(record["run_order"], 1)
        self.assertEqual(record["case"], "Copenhagen")
        self.assertIn("canonical native full headless run", record["scenario"])
        self.assertIs(record["command"], command)
        self.assertEqual(record["runtime_output"], "/tmp/measurement/runtime/Copenhagen")
        self.assertEqual(record["peak_rss_bytes"], 1048576)
        self.assertEqual(record["peak_rss_mib"], 1.0)
        self.assertEqual(record["elapsed_seconds"], 12.345679)
        self.assertEqual(record["exit_code"], 0)
        self.assertEqual(record["stdout_log"], "logs/Copenhagen.stdout.log")
        self.assertEqual(record["stderr_log"], "logs/Copenhagen.stderr.log")


if __name__ == "__main__":
    unittest.main()
