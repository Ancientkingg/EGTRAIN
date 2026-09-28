#!/usr/bin/env python3
import unittest

from version import parse_version, select_version


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


if __name__ == "__main__":
    unittest.main()
