#!/usr/bin/env python3
"""Print the output of the changes job of a workflow: whether a pull request needs the other jobs of the workflow.

usage: changed_files.py WORKFLOW BASE HEAD [--event NAME]

For a pull request (the default event) the script reads the files that changed from BASE to HEAD with
git diff --name-only --no-renames and prints one line NAME=true or NAME=false, the output of the workflow in the form
that $GITHUB_OUTPUT takes. Any other event, such as a nightly or a manual run, needs every job, so the output is true and
git is not run. A failing git command fails the script.

In the workflows the checkout is the merge commit of the pull request, whose first parent is the base branch, so they
pass HEAD^1 and HEAD. To see what a branch selects, pass the base branch and the branch, such as origin/main HEAD.
"""
import argparse
import fnmatch
import subprocess

# The rule of each workflow: the output of its changes job, how the patterns decide it, and the patterns. "only" needs the
# jobs when a changed file matches a pattern. "except" needs them unless every changed file matches one, so a pull request
# that changes no file needs them too. A pattern matches as in a shell case statement: * matches any text, slashes included.
RULES = {
    "cmake": ("build", "except", ("*.md", "docs/*", "LICENSE", ".github/ISSUE_TEMPLATE/*")),
    "format": (
        "format",
        "only",
        ("EGTRAIN/QEGTRAIN/*", ".clang-format", ".clang-format-ignore", "tools/format.py", ".github/workflows/format.yml"),
    ),
    "tidy": (
        "tidy",
        "only",
        ("EGTRAIN/QEGTRAIN/*", "CMakeLists.txt", ".clang-tidy", "tools/tidy.py", ".github/workflows/tidy.yml", ".github/actions/*"),
    ),
    "package-check": (
        "package",
        "only",
        (
            ".github/workflows/package-check.yml",
            ".github/workflows/package.yml",
            ".github/workflows/release.yml",
            ".github/actions/*",
            "tools/release/*",
            "tools/vcpkg/*",
            "installer/*",
            "Info.plist.in",
            "EGTRAIN/QEGTRAIN/update/*",
            "EGTRAIN/QEGTRAIN/app/main.cpp",
        ),
    ),
}


def needs_jobs(workflow: str, files: list) -> bool:
    """Whether a pull request that changes the files needs the jobs of the workflow."""
    _output, kind, patterns = RULES[workflow]
    matches = [any(fnmatch.fnmatchcase(file, pattern) for pattern in patterns) for file in files]
    return any(matches) if kind == "only" else not all(matches) or not files


def changed_files(base: str, head: str) -> list:
    command = ["git", "-c", "core.quotepath=false", "diff", "--name-only", "--no-renames", base, head]
    proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        raise SystemExit(f"{' '.join(command)} exited with {proc.returncode}\n{proc.stderr}")
    return [line for line in proc.stdout.split("\n") if line]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("workflow", choices=sorted(RULES))
    parser.add_argument("base")
    parser.add_argument("head")
    parser.add_argument("--event", default="pull_request", help="the event that started the workflow (GITHUB_EVENT_NAME)")
    args = parser.parse_args()
    needed = args.event != "pull_request" or needs_jobs(args.workflow, changed_files(args.base, args.head))
    print(f"{RULES[args.workflow][0]}={'true' if needed else 'false'}")


if __name__ == "__main__":
    main()
