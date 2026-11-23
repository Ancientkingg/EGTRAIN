#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path

from version import DEFAULT_NOTES, name_and_notes, notes_versions, parse_version, release_notes, select_version


class ReleaseVersionTests(unittest.TestCase):
    def test_first_production_update_is_newer_than_installed_baseline(self):
        self.assertEqual(select_version("1.0.0", ["production-111.1-11abfea"],
                                        "refs/heads/production"), "1.0.1")

    def test_production_increments_highest_stable_tag_numerically(self):
        self.assertEqual(select_version("1.0.0", ["v1.0.9", "v1.0.10", "v2.0.0-rc.1"],
                                        "refs/heads/production"), "1.0.11")

    def test_reserved_draft_version_is_not_reused(self):
        self.assertEqual(select_version("1.0.0", ["v1.0.1", "v1.0.2"],
                                        "refs/heads/production"), "1.0.3")

    def test_baseline_can_start_a_new_version_series(self):
        self.assertEqual(select_version("2.0.0", ["v1.0.10"],
                                        "refs/heads/production"), "2.0.1")

    def test_explicit_tags_set_the_packaged_version(self):
        self.assertEqual(select_version("1.0.0", ["v1.2.3"], "refs/tags/v1.2.3"), "1.2.3")
        self.assertEqual(select_version("1.0.0", [], "refs/tags/v1.2.3-rc.1"), "1.2.3")

    def test_non_publishing_runs_use_baseline(self):
        for ref in ("", "refs/pull/1/merge", "refs/heads/main"):
            self.assertEqual(select_version("1.0.0", ["v1.0.5"], ref), "1.0.0")

    def test_invalid_versions_are_rejected(self):
        for value in ("1.0", "01.0.0", "1.0.0.0", "1.0.0-rc.1", "1.0.65536"):
            with self.assertRaises(ValueError):
                parse_version(value)
        with self.assertRaises(ValueError):
            select_version("1.0.65535", [], "refs/heads/production")
        with self.assertRaises(ValueError):
            select_version("2.0.0", [], "refs/tags/v1.0.0")


    def test_notes_file_above_every_tag_sets_the_version(self):
        self.assertEqual(select_version("1.0.0", ["v1.0.9"], "refs/heads/production", ["1.1.0"]), "1.1.0")
        self.assertEqual(select_version("1.0.0", ["v1.0.9"], "refs/heads/production", ["1.2.0", "1.1.0"]), "1.1.0")

    def test_released_or_older_notes_files_fall_back_to_the_next_patch(self):
        self.assertEqual(select_version("1.0.0", ["v1.0.9", "v1.1.0"], "refs/heads/production", ["1.1.0"]), "1.1.1")
        self.assertEqual(select_version("1.0.0", ["v1.0.9"], "refs/heads/production", ["1.0.5"]), "1.0.10")

    def test_notes_files_do_not_change_runs_that_do_not_publish(self):
        self.assertEqual(select_version("1.0.0", ["v1.0.9"], "", ["1.1.0"]), "1.0.0")
        self.assertEqual(select_version("1.0.0", [], "refs/tags/v1.2.3", ["1.1.0"]), "1.2.3")

    def test_draft_of_a_named_release_stops_the_run(self):
        # A failed run left the draft v1.1.0: the retry must not publish an unnamed 1.1.1.
        with self.assertRaises(ValueError):
            select_version("1.0.0", ["v1.0.9", "v1.1.0"], "refs/heads/production", ["1.1.0"], ["v1.1.0"])
        self.assertEqual(select_version("1.0.0", ["v1.0.9", "v1.0.10"], "refs/heads/production", [], ["v1.0.10"]), "1.0.11")

    def test_notes_files_name_their_release(self):
        with tempfile.TemporaryDirectory() as folder:
            Path(folder, "1.1.0.md").write_text("# The Passengers Update\n\nPassengers ride the trains.\n", encoding="utf-8")
            self.assertEqual(notes_versions(Path(folder)), ["1.1.0"])
            self.assertEqual(name_and_notes("v1.1.0", Path(folder)),
                             ("EGTRAIN v1.1.0: The Passengers Update", "Passengers ride the trains.\n"))
            self.assertEqual(name_and_notes("v1.1.1", Path(folder)),
                             ("EGTRAIN v1.1.1", DEFAULT_NOTES.format(name="EGTRAIN v1.1.1")))
            self.assertEqual(name_and_notes("v1.1.0-rc.1", Path(folder))[0], "EGTRAIN v1.1.0-rc.1")
        self.assertEqual(notes_versions(Path("/nonexistent-notes-folder")), [])

    def test_malformed_notes_files_are_rejected(self):
        for name, text in (("1.1.md", "# T\n\nx\n"), ("1.1.0.txt", "# T\n\nx\n"), ("1.1.0.md", "Title\n\nx\n"),
                           ("1.1.0.md", "# \n\nx\n"), ("1.1.0.md", "# Title\n\n")):
            with self.subTest(name=name, text=text), tempfile.TemporaryDirectory() as folder:
                Path(folder, name).write_text(text, encoding="utf-8")
                with self.assertRaises(ValueError):
                    notes_versions(Path(folder))

    def test_notes_of_this_repository_are_valid(self):
        for version in notes_versions():
            release_notes(Path(__file__).resolve().parents[2] / "docs" / "releases" / f"{version}.md")

if __name__ == "__main__":
    unittest.main()
