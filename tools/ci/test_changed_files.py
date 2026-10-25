#!/usr/bin/env python3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from changed_files import RULES, needs_jobs

SCRIPT = Path(__file__).resolve().parent / "changed_files.py"

# Per workflow: file lists, each with whether a pull request that changes them needs the jobs.
CASES = {
    "cmake": [
        ([], True),
        (["README.md"], False),
        (["sub/dir/guide.md", "docs/x/y.txt", "LICENSE", ".github/ISSUE_TEMPLATE/bug.yml"], False),
        (["notes.mdx"], True),
        (["LICENSE.txt"], True),
        (["sub/LICENSE"], True),
        (["README.md", "EGTRAIN/QEGTRAIN/a.cpp"], True),
        ([".github/workflows/cmake.yml"], True),
        (["tools/ci/changed_files.py"], True),
    ],
    "format": [
        ([], False),
        (["EGTRAIN/QEGTRAIN/scene/b.h"], True),
        (["EGTRAIN/QEGTRAIN/notes.md"], True),
        ([".clang-format"], True),
        ([".clang-format-ignore"], True),
        ([".clang-format.bak", "tools/format.pyc", "README.md"], False),
        (["tools/format.py"], True),
        ([".github/workflows/format.yml"], True),
        ([".github/workflows/tidy.yml"], False),
    ],
    "tidy": [
        ([], False),
        (["EGTRAIN/QEGTRAIN/a.cpp"], True),
        (["EGTRAIN/QEGTRAIN/tests/.clang-tidy"], True),
        (["CMakeLists.txt"], True),
        (["sub/CMakeLists.txt", ".clang-tidy.bak", "tools/tidy.pyc", "docs/a.png"], False),
        ([".clang-tidy"], True),
        (["tools/tidy.py"], True),
        ([".github/workflows/tidy.yml"], True),
        ([".github/actions/setup-deps/action.yml"], True),
        ([".github/workflows/format.yml"], False),
    ],
    "package-check": [
        ([], False),
        ([".github/workflows/package-check.yml"], True),
        ([".github/workflows/package.yml"], True),
        ([".github/workflows/release.yml"], True),
        ([".github/actions/setup-deps/action.yml"], True),
        (["tools/release/version.py"], True),
        (["tools/release/x/y.sh"], True),
        (["tools/vcpkg/vcpkg.json"], True),
        (["installer/x/y.iss"], True),
        (["Info.plist.in"], True),
        (["EGTRAIN/QEGTRAIN/update/u.cpp"], True),
        (["EGTRAIN/QEGTRAIN/app/main.cpp"], True),
        (["EGTRAIN/QEGTRAIN/app/other.cpp", "sub/Info.plist.in", "tools/e2e/test_ci_workflow.py", "CMakeLists.txt"], False),
        ([".github/workflows/cmake.yml", "README.md"], False),
    ],
}


def git(repository: Path, *arguments: str) -> str:
    """Runs git in the repository with an identity of its own and without the signing and hooks of the machine."""
    settings = ("-c", "user.name=test", "-c", "user.email=test@example.invalid", "-c", "commit.gpgsign=false")
    return subprocess.run(["git", "-C", str(repository), *settings, *arguments], check=True, stdout=subprocess.PIPE,
                          encoding="utf-8").stdout.strip()


class ChangedFilesTests(unittest.TestCase):
    def test_the_rules_select_the_jobs_of_each_workflow(self):
        self.assertEqual(sorted(CASES), sorted(RULES))
        for workflow, cases in CASES.items():
            for files, needed in cases:
                with self.subTest(workflow=workflow, files=files):
                    self.assertEqual(needs_jobs(workflow, files), needed)

    def test_the_script_reads_the_changes_between_two_commits(self):
        with tempfile.TemporaryDirectory() as temp:
            repository = Path(temp)

            def commit(path: str) -> str:
                (repository / path).parent.mkdir(parents=True, exist_ok=True)
                (repository / path).write_text(path, encoding="utf-8")
                git(repository, "add", path)
                git(repository, "commit", "-q", "--no-verify", "-m", path)
                return git(repository, "rev-parse", "HEAD")

            def run(*arguments: str) -> subprocess.CompletedProcess:
                return subprocess.run([sys.executable, str(SCRIPT), *arguments], cwd=temp, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, encoding="utf-8")

            git(repository, "init", "-q")
            first = commit("README.md")
            documentation = commit("docs/guides/ä.md")
            source = commit("EGTRAIN/QEGTRAIN/a.cpp")
            for arguments, line in (
                (("cmake", first, documentation), "build=false"),
                (("cmake", first, source), "build=true"),
                (("tidy", documentation, source), "tidy=true"),
                (("package-check", first, source), "package=false"),
                (("format", first, documentation), "format=false"),
                (("cmake", first, documentation, "--event", "pull_request"), "build=false"),
                (("cmake", first, documentation, "--event", "schedule"), "build=true"),
                (("package-check", "no-such-commit", "HEAD", "--event", "workflow_dispatch"), "package=true"),
            ):
                with self.subTest(arguments=arguments):
                    proc = run(*arguments)
                    self.assertEqual((proc.returncode, proc.stdout), (0, line + "\n"), proc.stderr)
            failed = run("cmake", "no-such-commit", "HEAD")
            self.assertNotEqual(failed.returncode, 0)
            self.assertEqual(failed.stdout, "")
            self.assertIn("no-such-commit", failed.stderr)
            self.assertNotEqual(run("lint", first, source).returncode, 0)


if __name__ == "__main__":
    unittest.main()
