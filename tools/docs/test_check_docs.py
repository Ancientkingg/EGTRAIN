#!/usr/bin/env python3
import contextlib
import io
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from check_docs import Finding, ToolError, check, format_finding, main as run_main, tracked_files

LANDING = "docs/README.md"
EM_DASH = "\u2014"
EN_DASH = "\u2013"


def doc(*lines):
    return "\n".join(lines) + "\n"


class TreeTestCase(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def write(self, files):
        for name, content in files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content if isinstance(content, bytes) else content.encode("utf-8"))
        return sorted(files)

    def findings(self, files):
        return check(self.root, self.write(files))

    def found(self, files):
        return [(finding.path, finding.line, finding.kind) for finding in self.findings(files)]

    def landing(self, *lines):
        return self.found({LANDING: doc("# Documentation", "", *lines)})


class CleanTreeTests(TreeTestCase):
    def test_tree_without_findings(self):
        files = {
            "README.md": doc("# Project", "", "Start with [the documentation](docs/README.md)."),
            LANDING: doc(
                "# Documentation",
                "",
                "- [Guide](guides/guide.md)",
                "- [Reference](guides/reference.md#details)",
                "- [Site](https://example.com/page#top) and [mail](mailto:someone@example.com)",
                "- [Images](images)",
                "![Screenshot of the main window](images/main.png)",
            ),
            "docs/guides/guide.md": doc(
                "# Guide",
                "",
                "```bash",
                'tool validate --in "My Scene" \\',
                "  --verbose",
                "```",
                "",
                "```json",
                '{"scene": [1, 2]}',
                "```",
            ),
            "docs/guides/reference.md": doc("# Reference", "", "## Details", "", "Back to [the guide](guide.md)."),
            "docs/images/main.png": b"image",
        }
        self.assertEqual(self.found(files), [])


class KindTests(TreeTestCase):
    def test_broken_link_is_reported(self):
        self.assertEqual(self.landing("[gone](missing.md)"), [(LANDING, 3, "link")])

    def test_broken_anchor_is_reported(self):
        files = {LANDING: doc("# Docs", "", "[a](a.md#nothing)"), "docs/a.md": doc("# A")}
        self.assertEqual(self.found(files), [(LANDING, 3, "anchor")])

    def test_image_without_alt_text_is_reported(self):
        files = {LANDING: doc("# Docs", "", "![](images/p.png)"), "docs/images/p.png": b"image"}
        self.assertEqual(self.found(files), [(LANDING, 3, "image-alt")])

    def test_unreferenced_image_below_docs_is_reported(self):
        files = {LANDING: doc("# Docs"), "docs/images/unused.png": b"image"}
        self.assertEqual(self.found(files), [("docs/images/unused.png", 1, "orphan-image")])

    def test_image_suffix_is_compared_in_lower_case(self):
        files = {LANDING: doc("# Docs"), "docs/images/Shot.PNG": b"image"}
        self.assertEqual(self.found(files), [("docs/images/Shot.PNG", 1, "orphan-image")])

    def test_unreachable_document_is_reported(self):
        files = {LANDING: doc("# Docs"), "docs/lost.md": doc("# Lost")}
        self.assertEqual(self.found(files), [("docs/lost.md", 1, "unreachable")])

    def test_shell_syntax_error_is_reported_at_the_fence(self):
        self.assertEqual(self.landing("```bash", 'echo "open', "```"), [(LANDING, 3, "shell")])
        self.assertEqual(self.landing("```sh", "echo 'open", "```"), [(LANDING, 3, "shell")])

    def test_invalid_json_is_reported_at_the_fence(self):
        self.assertEqual(self.landing("```json", '{"a": 1,}', "```"), [(LANDING, 3, "json")])

    def test_em_dash_is_reported(self):
        files = {LANDING: doc("# Docs", "", f"one {EM_DASH} two")}
        self.assertEqual(self.found(files), [(LANDING, 3, "dash")])

    def test_en_dash_is_reported(self):
        files = {LANDING: doc("# Docs", "", f"1{EN_DASH}2")}
        self.assertEqual(self.found(files), [(LANDING, 3, "dash")])

    def test_users_path_is_reported(self):
        self.assertEqual(self.landing("Open /Users/someone/project."), [(LANDING, 3, "local-path")])

    def test_home_path_is_reported(self):
        self.assertEqual(self.landing("Open /home/someone/project."), [(LANDING, 3, "local-path")])

    def test_windows_drive_path_is_reported(self):
        self.assertEqual(self.landing("Open C:\\Users\\someone\\project."), [(LANDING, 3, "local-path")])

    def test_home_followed_by_a_space_is_not_a_local_path(self):
        self.assertEqual(self.landing("The directory /home/ is not a user directory."), [])

    def test_dashes_and_local_paths_are_found_inside_fences(self):
        findings = self.landing("```text", f"a {EM_DASH} b", "/Users/someone/project", "```")
        self.assertEqual(findings, [(LANDING, 4, "dash"), (LANDING, 5, "local-path")])

    def test_link_message_names_the_target_as_written(self):
        findings = self.findings({LANDING: doc("# Docs", "", "[x](../docs/Missing%20File.md#top)")})
        self.assertEqual(len(findings), 1)
        self.assertIn("../docs/Missing%20File.md#top", findings[0].message)


class ParserTests(TreeTestCase):
    def test_heading_with_backticks_and_dot_gives_anchor_without_dot(self):
        files = {
            LANDING: doc("# Docs", "", "[ok](a.md#viewsjson-and-runtime-metadata)", "[dot](a.md#views.json-and-runtime-metadata)"),
            "docs/a.md": doc("# A", "", "## `views.json` and runtime metadata"),
        }
        self.assertEqual(self.found(files), [(LANDING, 4, "anchor")])

    def test_repeated_heading_gets_numbered_anchor(self):
        files = {
            LANDING: doc(
                "# Docs", "", "[1](a.md#usage)", "[2](a.md#usage-1)", "[3](a.md#usage-2)", "[4](a.md#usage-3)"
            ),
            "docs/a.md": doc("# A", "", "## Usage", "", "## USAGE", "", "## usage"),
        }
        self.assertEqual(self.found(files), [(LANDING, 6, "anchor")])

    def test_hash_line_in_fence_is_not_an_anchor(self):
        files = {
            LANDING: doc("# Docs", "", "[ok](a.md#a)", "[fenced](a.md#not-a-heading)"),
            "docs/a.md": doc("# A", "", "```text", "# Not a heading", "```"),
        }
        self.assertEqual(self.found(files), [(LANDING, 4, "anchor")])

    def test_link_inside_fence_is_ignored(self):
        self.assertEqual(self.landing("```text", "[x](missing.md)", "```"), [])

    def test_link_inside_inline_code_span_is_ignored(self):
        findings = self.landing("Use `[x](missing.md)` and ``[y](gone.md)`` here.", "`code` [z](zzz.md)")
        self.assertEqual(findings, [(LANDING, 4, "link")])

    def test_percent_encoded_space_in_target(self):
        files = {
            LANDING: doc("# Docs", "", "[ok](a%20b.md#two%2Dwords)", "[gone](a%20c.md)"),
            "docs/a b.md": doc("# A", "", "## Two words"),
        }
        self.assertEqual(self.found(files), [(LANDING, 4, "link")])

    def test_fragment_is_compared_exactly(self):
        files = {
            LANDING: doc("# Docs", "", "[ok](a.md#foo)", "[short](a.md#fo)", "[long](a.md#foo-)"),
            "docs/a.md": doc("# A", "", "## Foo"),
        }
        self.assertEqual(self.found(files), [(LANDING, 4, "anchor"), (LANDING, 5, "anchor")])

    def test_heading_keeps_hyphens_underscores_digits_and_letters_beyond_ascii(self):
        files = {
            LANDING: doc(
                "# Docs",
                "",
                "[ok](a.md#peak-memory-v2_beta-caf\u00e9-3)",
                "[hyphen](a.md#peakmemory-v2_beta-caf\u00e9-3)",
                "[underscore](a.md#peak-memory-v2beta-caf\u00e9-3)",
                "[digit](a.md#peak-memory-v2_beta-caf\u00e9)",
                "[letter](a.md#peak-memory-v2_beta-cafe-3)",
            ),
            "docs/a.md": doc("# A", "", "## Peak-memory v2_beta caf\u00e9 (3)"),
        }
        self.assertEqual(self.found(files), [(LANDING, line, "anchor") for line in (4, 5, 6, 7)])

    def test_uppercase_fragment_is_a_finding(self):
        files = {LANDING: doc("# Docs", "", "[x](a.md#Foo)"), "docs/a.md": doc("# A", "", "## Foo")}
        self.assertEqual(self.found(files), [(LANDING, 3, "anchor")])

    def test_target_in_angle_brackets_may_hold_a_space(self):
        files = {LANDING: doc("# Docs", "", "[ok](<a b.md>)", "[gone](<c d.md>)"), "docs/a b.md": doc("# A")}
        self.assertEqual(self.found(files), [(LANDING, 4, "link")])

    def test_link_title_is_not_part_of_the_target(self):
        files = {LANDING: doc("# Docs", "", '[ok](a.md "A title")'), "docs/a.md": doc("# A")}
        self.assertEqual(self.found(files), [])

    def test_link_title_may_use_double_quotes_single_quotes_or_parentheses(self):
        findings = self.landing('[a](gone.md "Title")', "[b](lost.md 'Title')", "[c](none.md (Title))")
        self.assertEqual(findings, [(LANDING, 3, "link"), (LANDING, 4, "link"), (LANDING, 5, "link")])

    def test_link_text_that_is_a_code_span_is_still_a_link(self):
        self.assertEqual(self.landing("[`cmd`](gone.md)"), [(LANDING, 3, "link")])

    def test_image_with_a_code_span_as_alt_text_has_alt_text(self):
        self.assertEqual(self.landing("![`x`](https://example.com/p.png)"), [])

    def test_fragment_in_the_same_file_is_checked(self):
        self.assertEqual(self.landing("[ok](#documentation)", "[gone](#elsewhere)"), [(LANDING, 4, "anchor")])

    def test_fragment_on_a_target_that_is_not_markdown_is_not_checked(self):
        files = {LANDING: doc("# Docs", "", "[x](data.json#anything)"), "docs/data.json": "{}"}
        self.assertEqual(self.found(files), [])

    def test_reachability_is_transitive(self):
        files = {
            LANDING: doc("# Docs", "", "[a](a.md)"),
            "docs/a.md": doc("# A", "", "[b](b.md)"),
            "docs/b.md": doc("# B", "", "[c](sub/c.md)"),
            "docs/sub/c.md": doc("# C"),
            "docs/d.md": doc("# D"),
        }
        self.assertEqual(self.found(files), [("docs/d.md", 1, "unreachable")])

    def test_reachability_follows_links_through_a_document_outside_docs(self):
        files = {
            "README.md": doc("# Project", "", "[b](docs/b.md)"),
            LANDING: doc("# Docs", "", "[project](../README.md)"),
            "docs/b.md": doc("# B"),
            "docs/c.md": doc("# C"),
        }
        self.assertEqual(self.found(files), [("docs/c.md", 1, "unreachable")])

    def test_link_from_an_unreachable_document_does_not_make_a_document_reachable(self):
        files = {
            LANDING: doc("# Docs"),
            "docs/a.md": doc("# A", "", "[b](b.md)"),
            "docs/b.md": doc("# B"),
        }
        self.assertEqual(self.found(files), [("docs/a.md", 1, "unreachable"), ("docs/b.md", 1, "unreachable")])

    def test_link_above_the_root_is_a_finding(self):
        files = {
            "README.md": doc("# Project", "", "[up](../README.md)"),
            LANDING: doc("# Docs", "", "[root](../README.md)", "[up](../../README.md)"),
        }
        self.assertEqual(self.found(files), [("README.md", 3, "link"), (LANDING, 4, "link")])

    def test_heredoc_block_is_skipped_by_the_shell_check(self):
        findings = self.landing("```bash", "cat <<EOF", "it's a heredoc", "EOF", "```", "", "```bash", "echo it's", "```")
        self.assertEqual(findings, [(LANDING, 9, "shell")])

    def test_block_with_another_info_word_is_not_checked(self):
        lines = []
        for info in ("text", "powershell", "cmake", ""):
            lines += [f"```{info}", 'echo "open', "```", ""]
        self.assertEqual(self.landing(*lines), [])

    def test_command_continued_with_backslashes_is_one_command(self):
        findings = self.landing("```bash", "tool export \\", '  --out "My Dir" \\', "  scene", "```")
        self.assertEqual(findings, [])

    def test_backslash_at_the_end_of_a_block_is_reported(self):
        self.assertEqual(self.landing("```bash", "tool export \\", "```"), [(LANDING, 3, "shell")])

    def test_backslash_at_the_end_of_a_comment_does_not_continue_the_comment(self):
        self.assertEqual(self.landing("```bash", "# note \\", "echo 'open", "```"), [(LANDING, 3, "shell")])

    def test_escaped_backslash_at_the_end_of_a_line_does_not_continue_the_line(self):
        self.assertEqual(self.landing("```bash", "echo \\\\", '"open', "```"), [(LANDING, 3, "shell")])

    def test_shell_comment_may_contain_a_quote(self):
        self.assertEqual(self.landing("```bash", "# don't panic", "echo done", "```"), [])

    def test_missing_landing_page_gives_one_finding(self):
        files = {"docs/a.md": doc("# A"), "docs/b.md": doc("# B", "", "[a](a.md)")}
        findings = self.findings(files)
        self.assertEqual([(f.path, f.line, f.kind) for f in findings], [(LANDING, 1, "unreachable")])
        self.assertIn("landing page missing", findings[0].message)

    def test_shorter_fence_line_does_not_close_a_fence(self):
        findings = self.landing("````text", "```", "[x](missing.md)", "````", "[y](gone.md)")
        self.assertEqual(findings, [(LANDING, 7, "link")])

    def test_fence_of_the_other_character_does_not_close_a_fence(self):
        findings = self.landing("```text", "~~~", "[x](missing.md)", "```", "[y](gone.md)")
        self.assertEqual(findings, [(LANDING, 7, "link")])

    def test_fence_line_with_text_does_not_close_a_fence(self):
        findings = self.landing("```text", "```json", "[x](missing.md)", "```", "[y](gone.md)")
        self.assertEqual(findings, [(LANDING, 7, "link")])

    def test_indented_fence_is_closed_by_an_indented_line(self):
        findings = self.landing("- item", "", "  ```bash", "  echo 'open", "  [x](missing.md)", "  ```", "", "[y](gone.md)")
        self.assertEqual(findings, [(LANDING, 5, "shell"), (LANDING, 10, "link")])

    def test_heading_may_be_indented_by_up_to_three_spaces(self):
        files = {
            LANDING: doc("# Docs", "", "[three](a.md#three)", "[four](a.md#four)"),
            "docs/a.md": doc("# A", "", "   ## Three", "", "    ## Four"),
        }
        self.assertEqual(self.found(files), [(LANDING, 4, "anchor")])

    def test_backtick_fence_line_with_a_backtick_in_its_info_string_is_not_a_fence(self):
        findings = self.landing("```x``` see [a](gone.md)", "[b](lost.md)")
        self.assertEqual(findings, [(LANDING, 3, "link"), (LANDING, 4, "link")])

    def test_tilde_fence_line_may_have_a_backtick_in_its_info_string(self):
        self.assertEqual(self.landing("~~~text `x`", "[x](missing.md)", "~~~", "[y](gone.md)"), [(LANDING, 6, "link")])

    def test_lines_ending_in_carriage_return_and_line_feed(self):
        text = "# Docs\r\n\r\n```text\r\ncode\r\n```\r\n[ok](#docs)\r\n[gone](missing.md)\r\n"
        self.assertEqual(self.found({LANDING: text}), [(LANDING, 7, "link")])

    def test_tilde_fence_works(self):
        findings = self.landing("~~~bash", "echo it's", "[x](missing.md)", "~~~", "[y](gone.md)")
        self.assertEqual(findings, [(LANDING, 3, "shell"), (LANDING, 7, "link")])

    def test_unclosed_fence_runs_to_the_end_of_the_file(self):
        self.assertEqual(self.landing("```bash", 'echo "open', "", "[x](missing.md)"), [(LANDING, 3, "shell")])

    def test_closing_hashes_are_removed_and_a_hash_in_the_text_is_kept(self):
        files = {
            LANDING: doc("# Docs", "", "[go](a.md#go)", "[trailing](a.md#go-)", "[c](a.md#c)"),
            "docs/a.md": doc("# A", "", "## Go ##", "", "## C#"),
        }
        self.assertEqual(self.found(files), [(LANDING, 4, "anchor")])

    def test_heading_with_a_link_gives_the_anchor_of_the_link_text(self):
        files = {
            LANDING: doc("# Docs", "", "[ok](a.md#see-the-guide-now)", "[url](a.md#see-the-guidehttpsexamplecomg-now)"),
            "docs/a.md": doc("# A", "", "## See [the guide](https://example.com/g) now"),
        }
        self.assertEqual(self.found(files), [(LANDING, 4, "anchor")])

    def test_heading_with_emphasis_and_punctuation_gives_the_github_anchor(self):
        files = {
            LANDING: doc("# Docs", "", "[ok](a.md#run-simulation-and-save-as)"),
            "docs/a.md": doc("# A", "", "## **Run** simulation, and *Save* As?"),
        }
        self.assertEqual(self.found(files), [])

    def test_empty_link_target_is_a_finding(self):
        self.assertEqual(self.landing("[x]()"), [(LANDING, 3, "link")])

    def test_target_starting_with_a_slash_is_a_finding(self):
        files = {LANDING: doc("# Docs", "", "[x](/docs/a.md)"), "docs/a.md": doc("# A")}
        self.assertEqual(self.found(files), [(LANDING, 3, "link"), ("docs/a.md", 1, "unreachable")])

    def test_link_to_a_directory_that_holds_a_listed_file_is_accepted(self):
        files = {
            LANDING: doc("# Docs", "", "[dir](guides)", "[slash](guides/)", "[root](../)", "[file](guides/g.md)"),
            "docs/guides/g.md": doc("# G"),
        }
        self.assertEqual(self.found(files), [])

    def test_trailing_slash_on_a_file_is_a_finding(self):
        files = {
            LANDING: doc("# Docs", "", "[file](a.md/)", "[dir](guides/)", "[guide](guides/g.md)"),
            "docs/a.md": doc("# A"),
            "docs/guides/g.md": doc("# G"),
        }
        self.assertEqual(self.found(files), [(LANDING, 3, "link")])

    def test_link_to_an_empty_or_missing_directory_is_a_finding(self):
        (self.root / "docs" / "empty").mkdir(parents=True)
        self.assertEqual(self.landing("[empty](empty)", "[missing](nowhere)"), [(LANDING, 3, "link"), (LANDING, 4, "link")])

    def test_image_inside_link_text_gives_both_targets(self):
        findings = self.findings({LANDING: doc("# Docs", "", "[![shot](missing.png)](gone.md)")})
        self.assertEqual([(f.line, f.kind) for f in findings], [(3, "link"), (3, "link")])
        self.assertTrue(any("missing.png" in f.message for f in findings))
        self.assertTrue(any("gone.md" in f.message for f in findings))

    def test_image_with_empty_alt_text_is_reported(self):
        self.assertEqual(self.landing("![](https://example.com/p.png)"), [(LANDING, 3, "image-alt")])

    def test_image_with_only_spaces_as_alt_text_is_reported(self):
        self.assertEqual(self.landing("![   ](https://example.com/p.png)"), [(LANDING, 3, "image-alt")])

    def test_link_text_may_wrap_onto_the_next_line(self):
        self.assertEqual(self.landing("A [wrapped", "link](gone.md) is read."), [(LANDING, 3, "link")])

    def test_link_text_does_not_continue_over_a_blank_line(self):
        self.assertEqual(self.landing("A [text", "", "more](gone.md)"), [])

    def test_identical_findings_are_not_merged(self):
        self.assertEqual(self.landing("[x](gone.md) [x](gone.md)"), [(LANDING, 3, "link"), (LANDING, 3, "link")])

    def test_files_other_than_markdown_are_not_read(self):
        files = {LANDING: doc("# Docs", "", "![Data](data.bin)"), "docs/data.bin": b"\xff\xfe"}
        self.assertEqual(self.found(files), [])

    def test_invalid_utf8_in_a_markdown_file_raises_tool_error(self):
        files = {LANDING: doc("# Docs"), "docs/bad.md": b"# Bad\n\xff\n"}
        with self.assertRaises(ToolError) as context:
            self.findings(files)
        self.assertIn("docs/bad.md", str(context.exception))


class ReferenceTests(TreeTestCase):
    def test_image_referenced_by_a_plain_link_is_not_an_orphan(self):
        files = {LANDING: doc("# Docs", "", "[full size](images/p.png)"), "docs/images/p.png": b"image"}
        self.assertEqual(self.found(files), [])

    def test_image_inside_link_text_is_not_an_orphan(self):
        files = {
            LANDING: doc("# Docs", "", "[![Preview](images/p.png)](a.md)"),
            "docs/a.md": doc("# A"),
            "docs/images/p.png": b"image",
        }
        self.assertEqual(self.found(files), [])

    def test_image_referenced_from_an_unreachable_document_is_not_an_orphan(self):
        files = {
            LANDING: doc("# Docs"),
            "docs/old.md": doc("# Old", "", "![Old window](images/p.png)"),
            "docs/images/p.png": b"image",
        }
        self.assertEqual(self.found(files), [("docs/old.md", 1, "unreachable")])

    def test_image_referenced_only_from_the_root_readme_is_not_an_orphan(self):
        files = {
            "README.md": doc("# Project", "", "![Window](docs/images/p.png)"),
            LANDING: doc("# Docs"),
            "docs/images/p.png": b"image",
        }
        self.assertEqual(self.found(files), [])

    def test_image_outside_docs_is_not_an_orphan(self):
        files = {LANDING: doc("# Docs"), "assets/icon.png": b"image", "EGTRAIN/icons/mark.svg": "<svg/>"}
        self.assertEqual(self.found(files), [])

    def test_readme_and_files_outside_docs_are_not_subject_to_the_unreachable_check(self):
        files = {
            "README.md": doc("# Project"),
            LANDING: doc("# Docs"),
            "notes/todo.md": doc("# Todo"),
            ".github/ISSUE_TEMPLATE/bug.md": doc("# Bug"),
        }
        self.assertEqual(self.found(files), [])


class OutputTests(TreeTestCase):
    def test_findings_are_sorted_by_path_line_kind_and_message(self):
        files = {
            LANDING: doc(
                "# Docs",
                "",
                "[a](a.md) [gone](gone.md)",
                "see /Users/someone/x",
                f"one {EM_DASH} two [gone](gone2.md)",
            ),
            "docs/a.md": doc("# A", "", "[x](nothing.md)"),
            "docs/p.png": b"image",
        }
        listed = list(reversed(self.write(files)))
        findings = check(self.root, listed)
        self.assertEqual(findings, sorted(findings))
        self.assertEqual(
            [(f.path, f.line, f.kind) for f in findings],
            [
                (LANDING, 3, "link"),
                (LANDING, 4, "local-path"),
                (LANDING, 5, "dash"),
                (LANDING, 5, "link"),
                ("docs/a.md", 3, "link"),
                ("docs/p.png", 1, "orphan-image"),
            ],
        )

    def test_format_finding_prints_path_line_kind_and_message(self):
        finding = Finding("docs/a.md", 3, "link", "no such file or directory: x.md")
        self.assertEqual(format_finding(finding), "docs/a.md:3: [link] no such file or directory: x.md")


class GitOutputTests(TreeTestCase):
    def completed(self, returncode, stdout=b"", stderr=b""):
        return subprocess.CompletedProcess([], returncode, stdout, stderr)

    def test_name_that_git_lists_twice_is_returned_once(self):
        self.write({"a.md": doc("# A"), "b.md": doc("# B")})
        with mock.patch("check_docs.subprocess.run", return_value=self.completed(0, b"b.md\0a.md\0b.md\0")):
            self.assertEqual(tracked_files(self.root), ["a.md", "b.md"])

    def test_tool_error_message_holds_the_first_line_of_the_error_output(self):
        error = b"fatal: first problem\nhint: second line\n"
        with mock.patch("check_docs.subprocess.run", return_value=self.completed(128, stderr=error)):
            with self.assertRaises(ToolError) as context:
                tracked_files(self.root)
        self.assertIn("fatal: first problem", str(context.exception))
        self.assertNotIn("second line", str(context.exception))


@unittest.skipUnless(shutil.which("git"), "git is not installed")
class GitTests(TreeTestCase):
    def setUp(self):
        super().setUp()
        self.git("init", "-q")

    def git(self, *arguments):
        subprocess.run(["git", "-C", str(self.root), *arguments], check=True, capture_output=True)

    def call_main(self, root):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = run_main(["--root", str(root)])
        return code, out.getvalue(), err.getvalue()

    def test_untracked_markdown_file_is_listed(self):
        self.write({"docs/new.md": doc("# New")})
        self.assertEqual(tracked_files(self.root), ["docs/new.md"])

    def test_file_named_in_gitignore_is_not_listed(self):
        self.write({".gitignore": "ignored.md\n", "ignored.md": doc("# Ignored"), "kept.md": doc("# Kept")})
        self.assertEqual(tracked_files(self.root), [".gitignore", "kept.md"])

    def test_staged_file_deleted_from_the_disk_is_not_listed(self):
        self.write({"gone.md": doc("# Gone"), "kept.md": doc("# Kept")})
        self.git("add", "gone.md", "kept.md")
        (self.root / "gone.md").unlink()
        self.assertEqual(tracked_files(self.root), ["kept.md"])

    def test_listed_paths_use_forward_slashes_and_are_sorted(self):
        self.write({"docs/guides/g.md": doc("# G"), "docs/a.md": doc("# A"), "README.md": doc("# R")})
        self.assertEqual(tracked_files(self.root), ["README.md", "docs/a.md", "docs/guides/g.md"])

    def test_tracked_files_raises_tool_error_outside_a_git_repository(self):
        with tempfile.TemporaryDirectory() as directory, self.outside_any_repository(directory):
            with self.assertRaises(ToolError) as context:
                tracked_files(directory)
        self.assertIn("ls-files", str(context.exception))
        self.assertEqual(len(str(context.exception).splitlines()), 1)

    @unittest.skipIf(os.name == "nt", "Windows finds git outside PATH")
    def test_tracked_files_raises_tool_error_when_git_cannot_be_run(self):
        with mock.patch.dict(os.environ, {"PATH": str(self.root / "no-such-directory")}):
            with self.assertRaises(ToolError) as context:
                tracked_files(self.root)
        self.assertIn("cannot run", str(context.exception))

    def test_main_returns_0_and_prints_no_findings_for_a_clean_tree(self):
        self.write({LANDING: doc("# Docs", "", "[a](a.md)"), "docs/a.md": doc("# A")})
        self.assertEqual(self.call_main(self.root), (0, "no findings\n", ""))

    def test_main_returns_1_and_prints_the_findings(self):
        self.write({LANDING: doc("# Docs", "", "[a](gone.md)", "[b](lost.md)")})
        code, out, err = self.call_main(self.root)
        self.assertEqual(code, 1)
        self.assertEqual(err, "")
        lines = out.splitlines()
        self.assertEqual(lines[0], "docs/README.md:3: [link] no such file or directory: gone.md")
        self.assertEqual(lines[1], "docs/README.md:4: [link] no such file or directory: lost.md")
        self.assertEqual(lines[2:], ["2 findings"])

    def test_main_counts_one_finding_in_the_singular(self):
        self.write({LANDING: doc("# Docs", "", "[a](gone.md)")})
        code, out, _ = self.call_main(self.root)
        self.assertEqual(code, 1)
        self.assertEqual(out.splitlines()[-1], "1 finding")

    def test_main_escapes_characters_that_standard_output_cannot_encode(self):
        self.write({LANDING: doc("# Docs", "", "[a](caf\u00e9.md)")})
        raw = io.BytesIO()
        out = io.TextIOWrapper(raw, encoding="ascii")
        with contextlib.redirect_stdout(out):
            code = run_main(["--root", str(self.root)])
        out.flush()
        self.assertEqual(code, 1)
        self.assertIn(b"caf\\xe9.md", raw.getvalue())
        self.assertTrue(raw.getvalue().endswith(b"1 finding\n"))

    def test_main_returns_2_with_one_line_on_standard_error_outside_a_git_repository(self):
        with tempfile.TemporaryDirectory() as directory, self.outside_any_repository(directory):
            code, out, err = self.call_main(directory)
        self.assertEqual(code, 2)
        self.assertEqual(out, "")
        self.assertTrue(err.startswith("check_docs: "))
        self.assertEqual(len(err.splitlines()), 1)

    def test_main_returns_2_for_a_markdown_file_that_is_not_utf8(self):
        self.write({LANDING: doc("# Docs"), "docs/bad.md": b"# Bad\n\xff\n"})
        code, out, err = self.call_main(self.root)
        self.assertEqual(code, 2)
        self.assertEqual(out, "")
        self.assertTrue(err.startswith("check_docs: "))
        self.assertIn("docs/bad.md", err)
        self.assertEqual(len(err.splitlines()), 1)

    def outside_any_repository(self, directory):
        ceiling = str(Path(directory).resolve().parent)
        return mock.patch.dict(os.environ, {"GIT_CEILING_DIRECTORIES": ceiling})


if __name__ == "__main__":
    unittest.main()
