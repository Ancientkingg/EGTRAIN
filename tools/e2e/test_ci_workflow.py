#!/usr/bin/env python3
"""Checks the GitHub workflows for the properties that the required checks and the releases rely on.

The first checks hold for every workflow: the result jobs that main requires, the changed-files filter, time limits,
permissions, concurrency, credentials, the calls of the reusable package workflow and the files that the workflows name.
The later ones are the properties of the build, package and release jobs that docs/development describes. Steps are
found by what they run, not by their names.
"""
import json
import re
import subprocess
import sys
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]

# The condition of every job and step that signs, notarizes or publishes: a push to production or a v* tag.
PUSH_GATE = "github.event_name == 'push' && (github.ref == 'refs/heads/production' || startsWith(github.ref, 'refs/tags/v'))"
NIGHTLY_GATE = "github.event_name == 'schedule' || github.event_name == 'workflow_dispatch'"

# The workflows whose result job main requires: the name of that check, the entry of tools/ci/changed_files.py, the job
# that skips when the entry selects nothing, and the jobs of the nightly and manual runs, which the result job does not
# wait for.
REQUIRED = {
    "cmake.yml": ("CMake result", "cmake", "build", ("sanitizers", "thread-sanitizer")),
    "format.yml": ("Format result", "format", "check", ()),
    "tidy.yml": ("Tidy result", "tidy", "check", ()),
    "package-check.yml": ("Package check result", "package-check", "version", ()),
}

# The jobs that may write with the token of the run: the scopes of each, and why.
WRITE_PERMISSIONS = {
    ("docs.yml", "publish"): ({"contents"}, "pushes the documentation site to gh-pages"),
    ("release.yml", "release"): ({"contents"}, "creates and publishes the GitHub release"),
}

# Properties that these workflows do not have yet. The test fails when one of them gets the property, so that the entry
# goes with the change.
KNOWN_GAPS = {
    ("format.yml", "permissions"),
    ("format.yml", "concurrency"),
    ("format.yml", "changed-files script"),
    ("telemetry-contract.yml", "permissions"),
    ("telemetry-contract.yml", "concurrency"),
    ("telemetry-contract.yml", "time limit of check"),
    ("release.yml", "permissions"),
    ("release.yml", "time limit of version"),
    ("release.yml", "time limit of validation"),
    ("release.yml", "time limit of sanitizer"),
    ("release.yml", "time limit of release"),
    ("release.yml", "setup action"),
}


def workflow_text(name: str) -> str:
    return (ROOT / ".github/workflows" / name).read_text(encoding="utf-8")


def release_pipeline_text() -> str:
    """The release and package workflows and the macOS packaging script that the package workflow runs."""
    macos = (ROOT / "tools/release/package_macos.sh").read_text(encoding="utf-8")
    return workflow_text("release.yml") + "\n" + workflow_text("package.yml") + "\n" + macos


# A reader of the YAML that the workflows use: block mappings and sequences, flow sequences, block scalars and plain or
# quoted scalars on one line. Every scalar is a string, as yaml.BaseLoader reads it. It stops at an anchor, an alias, a
# tag, a flow mapping, a quoted item or a key in a flow sequence, text after a quoted value or a plain scalar over
# several lines, which the workflows do not use.
KEY = re.compile(r"^([^\s'\"#][^:#]*?|'[^']*'|\"[^\"]*\"):(?: +|$)(.*)$")
QUOTED = re.compile(r"""'(?:[^']|'')*'|"(?:[^"\\]|\\.)*\"""")
AFTER_VALUE = re.compile(r"\s*|\s+#.*")


def load_yaml(text: str):
    lines = text.splitlines()
    value, index = _node(lines, 0, 0)
    index = _skip(lines, index)
    if index < len(lines):
        raise ValueError(f"line {index + 1}: not read: {lines[index]!r}")
    return value


def _skip(lines: list[str], index: int) -> int:
    while index < len(lines) and (not lines[index].strip() or lines[index].lstrip().startswith("#")):
        index += 1
    return index


def _indent(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def _node(lines: list[str], index: int, indent: int):
    index = _skip(lines, index)
    if index >= len(lines) or _indent(lines[index]) < indent:
        return "", index
    indent = _indent(lines[index])
    if lines[index][indent:] == "-" or lines[index][indent:].startswith("- "):
        return _sequence(lines, index, indent)
    return _mapping(lines, index, indent)


def _sequence(lines: list[str], index: int, indent: int):
    items = []
    while True:
        index = _skip(lines, index)
        if index >= len(lines) or _indent(lines[index]) != indent or not lines[index][indent:].startswith("-"):
            return items, index
        rest = lines[index][indent + 1:]
        if not rest.strip():
            item, index = _node(lines, index + 1, indent + 1)
        elif KEY.match(rest.lstrip(" ")) or rest.lstrip(" ").startswith("- "):
            # The item starts on the line of the dash: read it as if the dash were a space.
            inner = indent + 1 + _indent(rest)
            lines[index] = " " * inner + rest.lstrip(" ")
            item, index = _node(lines, index, inner)
        else:
            item, index = _inline(rest), index + 1
        items.append(item)


def _mapping(lines: list[str], index: int, indent: int):
    mapping = {}
    while True:
        index = _skip(lines, index)
        if index >= len(lines) or _indent(lines[index]) != indent:
            return mapping, index
        match = KEY.match(lines[index][indent:])
        if not match:
            raise ValueError(f"line {index + 1}: no key in {lines[index]!r}")
        key, rest = _scalar(match.group(1)), match.group(2)
        if re.fullmatch(r"[|>][-+]?\s*(#.*)?", rest):
            mapping[key], index = _block_scalar(lines, index + 1, indent, rest[0], rest[1:2])
        elif not rest.strip() or rest.lstrip().startswith("#"):
            following = _skip(lines, index + 1)
            if following < len(lines) and (
                _indent(lines[following]) > indent
                or (_indent(lines[following]) == indent and lines[following][indent:].startswith("- "))
            ):
                mapping[key], index = _node(lines, following, _indent(lines[following]))
            else:
                mapping[key], index = "", index + 1
        else:
            mapping[key], index = _inline(rest), index + 1


def _block_scalar(lines: list[str], index: int, parent: int, style: str, chomp: str):
    body = []
    block_indent = None
    while index < len(lines):
        line = lines[index]
        if line.strip():
            if block_indent is None:
                block_indent = _indent(line)
            if block_indent <= parent or _indent(line) < block_indent:
                break
            body.append(line[block_indent:])
        else:
            body.append("")
        index += 1
    while body and not body[-1] and chomp != "+":
        body.pop()
    if not body:
        return "", index
    if style == "|":
        text = "\n".join(body)
    else:
        # Folded: a line break between two lines of text is a space. An empty line, and a line that is more indented
        # or follows one, keeps its line break.
        text = body[0]
        for previous, line in zip(body, body[1:]):
            if not line:
                text += "\n"
            elif line.startswith(" ") or previous.startswith(" ") or not previous:
                text += ("\n" if previous else "") + line
            else:
                text += " " + line
    return text + ("" if chomp == "-" else "\n"), index


def _inline(text: str):
    text = text.strip()
    if text[:1] in ("{", "&", "*", "!"):
        raise ValueError(f"unsupported YAML: {text!r}")
    if text.startswith("["):
        items, closed, rest = text[1:].partition("]")
        if not closed or not AFTER_VALUE.fullmatch(rest) or set(items) & set("'\"[{"):
            raise ValueError(f"unsupported YAML: {text!r}")
        items = [item.strip() for item in items.split(",") if item.strip()]
        # An item with an anchor, an alias, a tag or a key is not a plain string.
        if any(item[:1] in ("&", "*", "!", "?") or re.search(r":(\s|$)", item) for item in items):
            raise ValueError(f"unsupported YAML: {text!r}")
        return items
    if text[:1] in ("'", '"'):
        quoted = QUOTED.match(text)
        if not quoted or not AFTER_VALUE.fullmatch(text[quoted.end():]):
            raise ValueError(f"unsupported YAML: {text!r}")
        return _scalar(quoted.group(0))
    return _scalar(re.split(r"\s+#", text, maxsplit=1)[0])


def _scalar(text: str) -> str:
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] == "'":
        return text[1:-1].replace("''", "'")
    if len(text) >= 2 and text[0] == text[-1] == '"':
        return re.sub(r"\\(.)", lambda match: {"n": "\n", "t": "\t"}.get(match.group(1), match.group(1)), text[1:-1])
    return text


def flat(node) -> str:
    """The keys and the text of a node, one value a line, to search what a job or a step runs."""
    if isinstance(node, dict):
        return "\n".join(f"{key}: {flat(value)}" for key, value in node.items())
    if isinstance(node, list):
        return "\n".join(flat(item) for item in node)
    return node


def triggers(workflow: dict) -> dict:
    events = workflow.get("on", {})
    return {events: ""} if isinstance(events, str) else dict.fromkeys(events, "") if isinstance(events, list) else events


def needs(job: dict) -> list[str]:
    value = job.get("needs", [])
    return [value] if isinstance(value, str) else value


def runs(job: dict) -> str:
    """The shell of every step of a job."""
    return "\n".join(step.get("run", "") for step in job.get("steps", []))


def step_index(job: dict, marker: str) -> int:
    """The first step of a job that names the marker, or -1."""
    return next((index for index, step in enumerate(job.get("steps", [])) if marker in flat(step)), -1)


def step(job: dict, marker: str) -> dict:
    index = step_index(job, marker)
    return job["steps"][index] if index >= 0 else {}


def in_order(job: dict, *markers: str) -> bool:
    positions = [step_index(job, marker) for marker in markers]
    return -1 not in positions and positions == sorted(positions)


def configure_preset(presets: list[dict], name: str) -> dict:
    """The configure preset with the fields and cache variables that it inherits. Its own values win, then those of the
    earlier presets in inherits. An unknown name gives a preset with no cache variables."""
    preset = next((preset for preset in presets if preset["name"] == name), {})
    parents = preset.get("inherits", [])
    resolved: dict = {"cacheVariables": {}}
    for parent in reversed([parents] if isinstance(parents, str) else parents):
        inherited = configure_preset(presets, parent)
        resolved["cacheVariables"].update(inherited.pop("cacheVariables"))
        resolved.update(inherited)
    resolved["cacheVariables"].update(preset.get("cacheVariables", {}))
    resolved.update({key: value for key, value in preset.items() if key != "cacheVariables"})
    return resolved


def push_only_jobs(jobs: dict) -> set:
    """The jobs that only a push to production or a v* tag runs: a condition with the gate, or exactly the success of such
    a job. A condition that also accepts a skipped job holds for every pull request."""
    found = set()
    for job_id, job in jobs.items():
        condition = job.get("if", "")
        if PUSH_GATE in condition or any(condition == f"${{{{ needs.{other}.result == 'success' }}}}" for other in found):
            found.add(job_id)
    return found


def test_registration(cmake: str, name: str) -> str:
    """The egtrain_add_test call that registers the test, or an empty string."""
    found = re.search(rf"^\s*egtrain_add_test\({re.escape(name)}\b.*?\)$", cmake, re.MULTILINE | re.DOTALL)
    return found.group(0) if found else ""


def ctest_names(text: str) -> list[str]:
    """The test names of every ctest -R '^(a|b)$' in the text."""
    return [name for names in re.findall(r"-R '\^\(([\w|]+)\)\$'", text) for name in names.split("|")]


class Checks:
    """Collects the problems. A problem of a known gap is left out, and a known gap without a problem is one."""

    def __init__(self) -> None:
        self.problems: list[str] = []
        self.gaps: set[tuple[str, str]] = set()

    def expect(self, condition, problem: str, gap: tuple = None) -> None:
        if condition:
            return
        if gap in KNOWN_GAPS:
            self.gaps.add(gap)
        else:
            self.problems.append(problem)

    def finish(self) -> None:
        for workflow, gap in sorted(KNOWN_GAPS - self.gaps):
            self.problems.append(f"KNOWN_GAPS lists the {gap} of {workflow}, which it has now: remove the entry")
        if self.problems:
            raise SystemExit("These properties of the CI workflows do not hold:\n- " + "\n- ".join(self.problems))


def check_reader(checks: Checks) -> None:
    """The reader leaves out the comment after a value and stops at what it cannot read. CI has no PyYAML to compare with."""
    expect = checks.expect
    text = "a: 'x' # note\nb: \"y # z\" # note\nc: 'it''s'\nd: [e, f] # note\n"
    expect(load_yaml(text) == {"a": "x", "b": "y # z", "c": "it's", "d": ["e", "f"]}, "the YAML reader leaves out a comment")
    for text in ('a: ["b,c"]\n', "a: ['b', c]\n", "a: 'b' c\n", 'a: "b\n', "a: [b\n", "a: [b] c\n", "a: [{b: c}]\n", "a: [b: c]\n",
                 "a: [&b c]\n"):
        try:
            load_yaml(text)
        except ValueError:
            continue
        expect(False, f"the YAML reader stops at {text!r}")


def check_changed_file_names(checks: Checks, changed_files) -> None:
    """git prints U+0085, U+2028 and U+2029 in a file name as they are, so only a line feed ends a name."""
    listed = subprocess.CompletedProcess([], 0, stdout="docs/a\u0085b\u2028c\u2029d.md\n\nLICENSE\n", stderr="")
    with mock.patch.object(subprocess, "run", return_value=listed):
        names = changed_files.changed_files("base", "head")
    checks.expect(names == ["docs/a\u0085b\u2028c\u2029d.md", "LICENSE"], "tools/ci/changed_files.py splits the file names at line feeds only")


def check_required_workflows(checks: Checks, workflows: dict, rules: dict) -> None:
    """Every pull request to main starts the required workflows, and each reports one result job that main can require.
    A first job, changes, selects the other jobs with the entry of the workflow in tools/ci/changed_files.py."""
    expect = checks.expect
    for name, (check, rule, gated, nightly) in REQUIRED.items():
        workflow = workflows[name]
        events = triggers(workflow)
        expect(
            events.get("pull_request") == {"branches": ["main"]}
            and "push" not in events
            and not any(isinstance(event, dict) and {"paths", "paths-ignore"} & set(event) for event in events.values()),
            f"{name}: starts for every pull request to main, without a path filter, and not for a push",
        )
        job_ids = list(workflow["jobs"])
        result = workflow["jobs"].get("result", {})
        expect(job_ids[-1:] == ["result"] and result.get("name") == check and result.get("if") == "always()",
               f"{name}: {check} is the last job and always runs")
        expect(sorted(needs(result)) == sorted(job for job in job_ids if job not in ("result", *nightly)),
               f"{name}: {check} needs every job of a pull request")
        expect(all(workflow["jobs"].get(job, {}).get("if") == NIGHTLY_GATE for job in nightly),
               f"{name}: the jobs that {check} does not need run only at night or by hand")
        expect(
            any(
                step.get("if") == "contains(needs.*.result, 'failure') || contains(needs.*.result, 'cancelled')"
                and step.get("run", "").strip() == "exit 1"
                for step in result.get("steps", [])
            ),
            f"{name}: {check} fails when a job failed or was cancelled",
        )

        output, _kind, patterns = rules[rule]
        changes = workflow["jobs"].get("changes", {})
        select = next((step for step in changes.get("steps", []) if step.get("id") == "select"), {})
        expect(
            changes.get("runs-on") == "ubuntu-latest"
            and any(step.get("with", {}).get("fetch-depth") == "2" for step in changes.get("steps", []))
            and changes.get("outputs") == {output: f"${{{{ steps.select.outputs.{output} }}}}"},
            f"{name}: the changes job checks out the merge commit and its parent and has the output {output}",
        )
        call = f'python3 tools/ci/changed_files.py {rule} HEAD^1 HEAD --event "$GITHUB_EVENT_NAME" >> "$GITHUB_OUTPUT"'
        expect(select.get("run", "").strip() == call, f"{name}: the changes job runs {call}", gap=(name, "changed-files script"))
        if (name, "changed-files script") in KNOWN_GAPS:
            # Until then the case statement of the job has the patterns of the entry, and every other event selects.
            branches = re.findall(r"^ +(\S+)\) (\w+)=true ;;$", select.get("run", ""), re.MULTILINE)
            expect(
                tuple(pattern for pattern, _ in branches) == patterns
                and all(selected == output for _, selected in branches)
                and select.get("run", "").startswith(f'{output}=true\nif [ "$GITHUB_EVENT_NAME" = pull_request ]; then\n'),
                f"{name}: the case statement of the changes job has the patterns of the {rule} entry of tools/ci/changed_files.py",
            )

        # The gated job skips when the changes job selects nothing. A skipped matrix job would report under its unexpanded
        # name, so the CMake build starts its legs and skips every step but the failure upload.
        gate = f"needs.changes.outputs.{output} == 'true'"
        job = workflow["jobs"].get(gated, {})
        if "matrix" in job.get("strategy", {}):
            gated_steps = all(
                step.get("if", "") == gate or step.get("if", "").startswith(gate + " && ") or step.get("if") == "failure()"
                for step in job.get("steps", [])
            )
        else:
            gated_steps = job.get("if") == gate
        expect("changes" in needs(job) and gated_steps, f"{name}: {gated} runs only when the changes job selects it")


def check_every_workflow(checks: Checks, workflows: dict) -> None:
    """Time limits, permissions, concurrency, schedules and the conditions of the jobs, in every workflow."""
    expect = checks.expect
    schedules = {}
    for name, workflow in workflows.items():
        events = triggers(workflow)
        # A job that calls a reusable workflow cannot set a time limit; the jobs of that workflow have theirs.
        for job_id, job in workflow["jobs"].items():
            if "uses" not in job:
                expect(re.fullmatch(r"\d+", job.get("timeout-minutes", "")), f"{name}: job {job_id} has a timeout-minutes",
                       gap=(name, f"time limit of {job_id}"))
        top = workflow.get("permissions")
        expect(
            top == "read-all" or isinstance(top, dict) and top and all(level in ("read", "none") for level in top.values()),
            f"{name}: top-level permissions that only read",
            gap=(name, "permissions"),
        )
        for job_id, job in workflow["jobs"].items():
            permissions = job.get("permissions", {})
            allowed = WRITE_PERMISSIONS.get((name, job_id), (set(), ""))[0]
            expect(isinstance(permissions, dict) and {scope for scope, level in permissions.items() if level == "write"} == allowed,
                   f"{name}: job {job_id} has a mapping of permissions that writes the scopes that WRITE_PERMISSIONS lists for it")
        # A newer push to a pull request replaces the running checks of that pull request; a scheduled, manual or push
        # run has a group of its own. The release workflow has a publication group of its own.
        if "pull_request" in events and name != "release.yml":
            concurrency = workflow.get("concurrency", {})
            expect(
                isinstance(concurrency, dict)
                and concurrency.get("group", "").endswith("${{ github.event_name == 'pull_request' && github.ref || github.run_id }}")
                and concurrency.get("cancel-in-progress") == "${{ github.event_name == 'pull_request' }}",
                f"{name}: a concurrency group in which only a newer push to the same pull request cancels a run",
                gap=(name, "concurrency"),
            )
        # A scheduled run starts at a minute that is not on the hour, once a night, and not with another workflow.
        if "schedule" in events:
            crons = [entry.get("cron", "") for entry in events["schedule"]]
            nightly = re.fullmatch(r"(\d+) (\d+) \* \* \*", crons[0]) if len(crons) == 1 else None
            expect(nightly and nightly.group(1) != "0", f"{name}: one nightly run at a minute that is not on the hour")
            for cron in crons:
                schedules.setdefault(cron, []).append(name)
        expect(events.get("workflow_dispatch", "") == "", f"{name}: a manual start without inputs")
        expect("continue-on-error" not in flat(workflow), f"{name}: no step or job that ignores a failure")
    for cron, names in schedules.items():
        expect(len(names) == 1, f"{', '.join(names)}: start at the same time ({cron})")


def check_credentials(checks: Checks, workflows: dict) -> None:
    """A credential reaches only the steps of a push to production or a v* tag: through a job or a step with that
    condition, or a job that runs only after such a job succeeded. Every other key of a job applies to the whole job, and
    the top-level env to every job. The secrets of a call of a workflow of the repository are the exception: this check
    reads the jobs of that workflow in turn. A call passes a credential by name, never all of them, and no step that
    reads one traces its commands."""
    expect = checks.expect
    for name, workflow in workflows.items():
        push_jobs = push_only_jobs(workflow["jobs"])
        expect("secrets." not in flat(workflow.get("env", {})), f"{name}: no credential in the top-level env, which reaches every job")
        for job_id, job in workflow["jobs"].items():
            local_call = re.fullmatch(r"\./\.github/workflows/[^/]+\.yml", job.get("uses", ""))
            settings = {key: value for key, value in job.items() if key not in ("steps", "if") and not (key == "secrets" and local_call)}
            expect("secrets." not in flat(settings) or job_id in push_jobs,
                   f"{name}: job {job_id} reads a credential outside its steps and a pull request runs it")
            if "uses" in job:
                passed = job.get("secrets", {})
                expect(
                    passed != "inherit"
                    and all(value == f"${{{{ secrets.{secret} }}}}" for secret, value in dict(passed or {}).items()),
                    f"{name}: job {job_id} passes each credential by its own name",
                )
                continue
            environment = job.get("environment", "")
            expect(not environment or job_id in push_jobs or PUSH_GATE in environment,
                   f"{name}: job {job_id} uses an environment only for a push to production or a v* tag")
            for item in job.get("steps", []):
                reads = "secrets." in flat({key: value for key, value in item.items() if key != "if"})
                expect(not reads or job_id in push_jobs or PUSH_GATE in item.get("if", ""),
                       f"{name}: job {job_id} reads a credential in a step that a pull request runs")
                expect(not reads or not re.search(r"\bset\s+-\w*x", item.get("run", "")),
                       f"{name}: job {job_id} traces the commands of a step that reads a credential")
                expect("secrets." not in item.get("if", ""), f"{name}: job {job_id} has a step condition that reads a credential")


def check_reusable_calls(checks: Checks, workflows: dict) -> None:
    """A job that calls a workflow of the repository passes the inputs that the workflow declares, its required ones
    included, only the credentials that it declares, and grants every permission that its jobs ask for: a called
    workflow cannot hold more than its caller grants."""
    expect = checks.expect
    for name, workflow in workflows.items():
        for job_id, job in workflow["jobs"].items():
            target = re.fullmatch(r"\./\.github/workflows/(.+)", job.get("uses", ""))
            if not target:
                continue
            called = workflows.get(target.group(1), {})
            declared = triggers(called).get("workflow_call") or {}
            inputs = declared.get("inputs", {}) if isinstance(declared, dict) else {}
            passed = job.get("with", {})
            expect(
                set(passed) <= set(inputs) and {key for key, value in inputs.items() if value.get("required") == "true"} <= set(passed),
                f"{name}: job {job_id} passes the inputs that {target.group(1)} declares",
            )
            expect(set(job.get("secrets") or {}) <= set(declared.get("secrets", {}) if isinstance(declared, dict) else {}),
                   f"{name}: job {job_id} passes only the credentials that {target.group(1)} declares")
            asked = [called.get("permissions", {})] + [other.get("permissions", {}) for other in called.get("jobs", {}).values()]
            granted = job.get("permissions", {})
            expect(
                isinstance(granted, dict)
                and all(granted.get(scope) in (level, "write") for block in asked if isinstance(block, dict) for scope, level in block.items()),
                f"{name}: job {job_id} grants the permissions that the jobs of {target.group(1)} ask for",
            )


def check_references(checks: Checks, texts: dict, tests_cmake: str, characterization_cmake: str) -> None:
    """The files that the workflows and the setup action name exist, the tests that they select are registered, and every
    test script of tools/ci and tools/release is registered in CTest or run by a workflow."""
    expect = checks.expect
    everything = "\n".join(texts.values())
    for name, text in texts.items():
        for action in re.findall(r"uses: (\./\S+)", text):
            expect((ROOT / action).is_file() or (ROOT / action / "action.yml").is_file(), f"{name}: {action} exists")
        for path in sorted(set(re.findall(r"(?<![\w./-])tools/[\w./*$-]*[\w*]", text))):
            expect("*" in path or "$" in path or (ROOT / path).exists(), f"{name}: {path} exists")
        for test in ctest_names(text):
            expect(test_registration(tests_cmake, test) or f"add_test(NAME {test}\n" in characterization_cmake,
                   f"{name}: the test {test} that it runs is registered")
    for script in sorted((ROOT / "tools/ci").glob("test_*")) + sorted((ROOT / "tools/release").glob("test_*")):
        path = script.relative_to(ROOT).as_posix()
        expect(f"${{CMAKE_CURRENT_SOURCE_DIR}}/{path}" in tests_cmake or path in everything,
               f"{path} is registered in CTest or run by a workflow")


def check_dependency_setup(checks: Checks, workflows: dict, texts: dict, action: dict) -> None:
    """Qt 6.8.3 and the other libraries come from the setup action, and every job that installs Qt gets the same version."""
    expect = checks.expect
    install = next((step for step in action["runs"]["steps"] if step.get("uses", "").startswith("jurplel/install-qt-action@")), {})
    expect(install.get("with", {}) == {"version": "6.8.3", "modules": "qtcharts", "cache": "true"},
           "setup-deps: Qt 6.8.3 with Qt Charts from install-qt-action, cached")
    expect("ilammy/msvc-dev-cmd@v1" in flat(action) and "brew install zeromq cppzmq nlohmann-json" in flat(action)
           and "apt-get install -y libzmq3-dev cppzmq-dev nlohmann-json3-dev $APT_PACKAGES" in flat(action),
           "setup-deps: the MSVC environment, and ZeroMQ, cppzmq and nlohmann-json from Homebrew and apt")
    for name, workflow in workflows.items():
        for job_id, job in workflow["jobs"].items():
            for item in job.get("steps", []):
                installs = item.get("uses", "").startswith("jurplel/install-qt-action@")
                expect(not installs, f"{name}: job {job_id} installs Qt with the setup action", gap=(name, "setup action"))
                expect(not installs or item.get("with", {}).get("version") == install.get("with", {}).get("version"),
                       f"{name}: job {job_id} installs the Qt version of the setup action")
    qt5 = re.compile(r"qt@5|qt5|Qt5|qtbase5|5\.15|--no-angle|--no-virtualkeyboard|qwindowsvistastyle|inputs\.qt|matrix\.qt|ARTIFACT_SUFFIX")
    for name, text in texts.items():
        found = qt5.search(text)
        expect(not found, f"{name}: no Qt 5 and no choice of Qt version, but it has {found.group(0) if found else ''}")


def check_build(checks: Checks, cmake: dict, cmake_text: str, presets: list, package: dict, release_text: str, cmake_lists: str) -> None:
    """The three legs of the CMake workflow, and what they share with the package jobs."""
    expect = checks.expect
    build = cmake["jobs"]["build"]
    legs = {leg["name"]: leg for leg in build["strategy"]["matrix"]["include"]}
    expect(
        {name: leg["os"] for name, leg in legs.items()} == {"macOS": "macos-latest", "Windows": "windows-latest", "Linux": "ubuntu-24.04"}
        and build["strategy"].get("fail-fast") == "false"
        and {name: leg.get("cache") for name, leg in legs.items()} == {"macOS": "ccache", "Windows": "sccache", "Linux": "ccache"},
        "cmake.yml: independent macOS, Windows and Linux legs, with ccache, sccache and ccache",
    )
    configure = {preset: step(build, f"cmake --preset {preset} ") for preset in ("ci-macos", "ci-linux", "ci-windows")}
    launcher = "-DCMAKE_C_COMPILER_LAUNCHER=${{ matrix.cache }} -DCMAKE_CXX_COMPILER_LAUNCHER=${{ matrix.cache }}"
    for preset, item in configure.items():
        variables = configure_preset(presets, preset)["cacheVariables"]
        run = item.get("run", "")
        # The configure line keeps the generator and the cache variables of its preset.
        overrides = re.search(r"(^|\s)[\"']?-G", run) or any(re.search(rf"-D\s*[\"']?{re.escape(name)}(:\w+)?=", run) for name in variables)
        expect(
            item and launcher in " ".join(run.split())
            and not overrides
            and configure_preset(presets, preset).get("binaryDir") == "${sourceDir}/build"
            and variables.get("CMAKE_BUILD_TYPE") == "Release"
            and variables.get("EGTRAIN_WARNINGS_AS_ERRORS") == "ON"
            and variables.get("EGTRAIN_BUILD_TESTS") == "ON"
            and variables.get("EGTRAIN_IPO") == "OFF",
            f"cmake.yml: a configure step with the preset {preset}, which keeps its generator and cache variables: a Release "
            "build with tests, warnings as errors, no interprocedural optimization, the compiler cache, in build/",
        )
    # A compiler cache cannot store objects with interprocedural optimization, which the package and release builds keep.
    expect("EGTRAIN_IPO" not in release_text and re.search(r'^option\(EGTRAIN_IPO ".*" ON\)$', cmake_lists, re.MULTILINE),
           "interprocedural optimization on by default and left alone by the package and release workflows")
    windows_leg = configure_preset(presets, "ci-windows")
    windows_preset = windows_leg["cacheVariables"]
    expect(
        windows_leg.get("generator") == "Ninja"
        and windows_preset.get("CMAKE_C_COMPILER") == windows_preset.get("CMAKE_CXX_COMPILER") == "cl"
        and "build/Release" not in flat(build),
        "cmake.yml: the ci-windows preset builds with Ninja and cl, so that sccache works, and its programs are not in build/Release",
    )
    cache = step(build, "hendrikmuhs/ccache-action@")
    expect(
        cache.get("with", {}).get("variant") == "${{ matrix.cache }}"
        and cache.get("with", {}).get("key", "").startswith("${{ matrix.name }}")
        and all(step_index(build, "hendrikmuhs/ccache-action@") < step_index(build, f"cmake --preset {preset} ") for preset in configure),
        "cmake.yml: the compiler cache of each leg is set up before its configure step",
    )
    test = step(build, "ctest --test-dir build -C Release --parallel ${{ matrix.jobs }}")
    expect(
        "cmake --build build --config Release" in runs(build)
        and all(part in test.get("run", "") for part in ("--output-on-failure", "--output-log", "-LE network")),
        "cmake.yml: each leg builds and runs CTest in parallel with a log, without the network tests",
    )
    # The network tests are built for the Windows leg only, and run in a step of their own after the other tests.
    https = step(build, "ctest --test-dir build -C Release -L network --no-tests=error")
    expect(
        windows_preset.get("EGTRAIN_NETWORK_TESTS") == "ON"
        and not any("EGTRAIN_NETWORK_TESTS" in configure_preset(presets, preset)["cacheVariables"] for preset in ("ci-macos", "ci-linux"))
        and "EGTRAIN_NETWORK_TESTS" not in cmake_text
        and "runner.os == 'Windows'" in https.get("if", "")
        and in_order(build, "--output-log", "-L network --no-tests=error"),
        "cmake.yml: the Windows HTTPS check after the tests, with the network tests of the Windows leg only",
    )
    # The vcpkg binary cache of the Windows leg is keyed by the manifest, the runner image and a manual version. The save
    # step follows the configure step that installs the packages and runs when the restore missed the key.
    restore = step(build, "actions/cache/restore@")
    save = step(build, "actions/cache/save@")
    key = restore.get("with", {}).get("key", "")
    expect(
        re.fullmatch(r"vcpkg-x64-windows-\$\{\{ steps\.vcpkg-manifest\.outputs\.key \}\}-\$\{\{ steps\.image\.outputs\.id \}\}-v\d+", key)
        and save.get("with", {}).get("key") == key
        and "cache-hit != 'true'" in save.get("if", "")
        and in_order(build, "Get-Content tools/vcpkg/vcpkg.json", "actions/cache/restore@", "cmake --preset ci-windows ", "actions/cache/save@")
        and in_order(build, "$env:ImageVersion", "actions/cache/restore@")
        and "VCPKG_BINARY_SOURCES" in flat(configure["ci-windows"]),
        "cmake.yml: the vcpkg binary cache keyed by the manifest, the image and a version, saved after the configure step",
    )
    # The Windows leg and the Windows package take the vcpkg options from the ci-windows preset. vcpkg copies no DLL next
    # to each program; the leg puts the DLL folders on PATH.
    windows_vcpkg = {
        "CMAKE_TOOLCHAIN_FILE": ("$env{VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake", '"$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"'),
        "VCPKG_TARGET_TRIPLET": ("x64-windows", "x64-windows"),
        "VCPKG_MANIFEST_DIR": ("${sourceDir}/tools/vcpkg", '"$env:GITHUB_WORKSPACE/tools/vcpkg"'),
        "VCPKG_INSTALLED_DIR": ("$env{VCPKG_INSTALLATION_ROOT}/installed", '"$env:VCPKG_INSTALLATION_ROOT/installed"'),
    }
    package_configure = runs(package["jobs"]["package-windows"])
    expect(
        all(windows_preset.get(name) == preset_value and f"-D{name}={flag_value}" in package_configure
            for name, (preset_value, flag_value) in windows_vcpkg.items())
        and windows_preset.get("VCPKG_APPLOCAL_DEPS") == "OFF"
        and 'installed/x64-windows/bin" | Out-File -Append' in runs(build)
        and "vcpkg install" not in cmake_text + release_text,
        "cmake.yml: the vcpkg options of the ci-windows preset in the Windows package job, and the DLL folders on PATH",
    )
    # The Windows leg assembles a package as the package job does, with the same windeployqt line, and starts it.
    start = step(build, "tools/release/package_start_smoke.py $dist --exports").get("run", "")
    deploy = [line.strip() for line in runs(package["jobs"]["package-windows"]).splitlines() if "windeployqt.exe" in line]
    expect(
        len(deploy) == 1 and [line.strip() for line in start.splitlines() if "windeployqt.exe" in line] == deploy
        and all(part in start and part in release_text for part in ('installed/x64-windows/bin/*.dll" $dist/', 'Copy-Item -Recurse EGTRAIN/QEGTRAIN/Scenes "$dist/Scenes"')),
        "cmake.yml: the Windows start check assembles the package with the windeployqt line and the commands of the package job",
    )
    # A failed leg uploads the CTest log, the GUI smoke log and the folders of the smokes. CTest writes below runner.temp.
    upload = next((item for item in build["steps"] if item.get("if") == "failure()"), {})
    expect(
        upload.get("uses", "").startswith("actions/upload-artifact@")
        and all(path in upload.get("with", {}).get("path", "") for path in (
            "/ctest.log", "qegtrain-gui-autostart-smoke.log", "${{ runner.temp }}/qegtrain-*-e2e.*/\n", "build/smoke-logs/headless_case_*.log"))
        and all('echo "TMPDIR=$RUNNER_TEMP" >> "$GITHUB_ENV"' in runs(cmake["jobs"][job]) for job in ("build", "sanitizers", "thread-sanitizer")),
        "cmake.yml: a failed leg uploads the logs that CTest and the smokes write below runner.temp",
    )
    docs = cmake["jobs"]["docs"]
    expect("python3 tools/docs/check_docs.py" in runs(docs) and "if" not in docs, "cmake.yml: the documentation check on every pull request")
    smokes = ("headless_smoke.py", "editor_smoke.sh", "roundtrip_smoke.py", "bundle_smoke.py", "assignment_smoke.py",
              "incident_smoke.py", "scene_render_smoke.sh", "track_preview_smoke.sh", "visual_polish_smoke.sh")
    expect(not any(smoke in cmake_text + release_text for smoke in smokes), "no smoke step outside CTest, which runs them all")


def check_sanitizers(checks: Checks, cmake: dict, release: dict, tests_cmake: str, characterization_cmake: str) -> None:
    """The leak check and the nightly sanitizer jobs build in Debug with the sanitizers, run only at night or by hand on
    Linux, and keep the options and the environment of the sanitizer job of the release workflow."""
    expect = checks.expect
    jobs = cmake["jobs"]
    leak, nightly, thread = jobs["leak-check"], jobs["sanitizers"], jobs["thread-sanitizer"]
    for job in (leak, nightly, thread):
        expect(job.get("runs-on") == "ubuntu-24.04" and job.get("if") == NIGHTLY_GATE and job.get("steps", [{}])[0].get("uses", "").startswith("actions/checkout@")
               and "./.github/actions/setup-deps" in flat(job) and '-DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"' in runs(job),
               "cmake.yml: the leak check and the sanitizer jobs on Linux, at night or by hand, with the setup action")
    debug_asan = "-DCMAKE_BUILD_TYPE=Debug -DEGTRAIN_BUILD_TESTS=ON -DEGTRAIN_ENABLE_SANITIZERS=ON"
    expect(
        debug_asan in runs(leak) and debug_asan in runs(nightly)
        and "-DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread" in runs(thread)
        and "-DCMAKE_BUILD_TYPE=Debug" in runs(thread)
        and "SANITIZERS=ON" not in runs(jobs["build"]) and "-fsanitize" not in runs(jobs["build"]),
        "cmake.yml: sanitizers only in the Debug builds of the leak check and the sanitizer jobs",
    )
    expect(leak.get("env", {}).get("ASAN_OPTIONS") == "detect_leaks=1" and "suppressions=" in leak.get("env", {}).get("LSAN_OPTIONS", ""),
           "cmake.yml: the leak check detects leaks, with its suppressions")
    expect({"characterization_repeatable", "characterization_repeat_paimpol", "test_networkpresenter", "test_runtimecanvas"} <= set(ctest_names(runs(leak))),
           "cmake.yml: the leak check reloads scenes and rebuilds the canvas")
    targets = re.search(r"cmake --build build --target ([\w ]+)", runs(leak))
    expect(targets and {"test_networkpresenter", "test_runtimecanvas"} <= set(targets.group(1).split()),
           "cmake.yml: the leak check builds the presenter tests that it runs")
    thread_targets = " ".join(step(thread, "cmake --build build --target").get("run", "").split())
    expect(
        {"test_recoverycontroller", "test_telemetrysender", "test_liveserver", "test_simulationworker"} <= set(ctest_names(runs(thread)))
        and all(name in thread_targets for name in ctest_names(runs(thread)))
        and "--no-tests=error" in runs(thread)
        and "suppressions=" in thread.get("env", {}).get("TSAN_OPTIONS", ""),
        "cmake.yml: the thread sanitizer job builds and runs the tests of the threaded parts, with its suppressions",
    )
    for job in (leak, nightly, thread):
        expect(any(item.get("uses", "").startswith("actions/upload-artifact@") and item.get("if") in ("failure()", "${{ !cancelled() }}")
                   and "runner.temp" in flat(item) for item in job["steps"]),
               "cmake.yml: the leak check and the sanitizer jobs upload their logs after a failure")
    # The nightly job and the release sanitizer job configure with the same options. The nightly job adds two ASAN options
    # and the UBSan suppressions to the environment of the release job and runs as many tests at once as the Linux leg.
    plain_option = r"-D[A-Za-z_]+=[\w.]+(?=\s)"
    release_job = release["jobs"]["sanitizer"]
    release_configure = step(release_job, "EGTRAIN_ENABLE_SANITIZERS=ON").get("run", "")
    nightly_configure = step(nightly, "EGTRAIN_ENABLE_SANITIZERS=ON").get("run", "")
    additions = {"ASAN_OPTIONS": ":detect_leaks=0:detect_stack_use_after_return=0",
                 "UBSAN_OPTIONS": ":suppressions=${{ github.workspace }}/tools/e2e/ubsan_suppressions.txt"}
    release_test = step(release_job, "ctest --test-dir build --parallel")
    nightly_test = step(nightly, "ctest --test-dir build --parallel")
    linux_jobs = next((leg["jobs"] for leg in jobs["build"]["strategy"]["matrix"]["include"] if leg["name"] == "Linux"), "")
    expect(
        set(re.findall(plain_option, release_configure + "\n")) == set(re.findall(plain_option, nightly_configure + "\n")) != set()
        and nightly_test.get("env") == {key: value + additions.get(key, "") for key, value in release_test.get("env", {}).items()}
        and set(additions) <= set(release_test.get("env", {}))
        and f"ctest --test-dir build --parallel {linux_jobs} --timeout 1500" in nightly_test.get("run", ""),
        "cmake.yml: the nightly sanitizer job has the options and the environment of the release sanitizer job",
    )
    # A sanitizer takes the last value of an option.
    halts = [[item.split("=", 1)[1] for item in release_test.get("env", {}).get(option, "").split(":") if item.startswith("halt_on_error=")]
             for option in ("UBSAN_OPTIONS", "ASAN_OPTIONS")]
    expect(all(values[-1:] == ["1"] for values in halts), "release.yml: the sanitizer job stops a test at the first finding")
    # An instrumented build runs slower: the release sanitizer job scales the time limits and runs as many tests at once as
    # the macOS leg. Only the sanitizer jobs pass the scale, so every other build keeps its limits.
    macos_jobs = next((leg["jobs"] for leg in jobs["build"]["strategy"]["matrix"]["include"] if leg["name"] == "macOS"), "")
    scale = re.search(r"-DEGTRAIN_TEST_TIMEOUT_SCALE=(\d+)", release_configure)
    scene = re.search(r"-DEGTRAIN_CHARACTERIZATION_SCENE_TIMEOUT=(\d+)", release_configure)
    expect(
        f"ctest --test-dir build --parallel {macos_jobs} --output-on-failure" in release_test.get("run", "")
        and scale and int(scale.group(1)) >= 2 and scene and int(scene.group(1)) > 300
        and 'set(EGTRAIN_TEST_TIMEOUT_SCALE 1 CACHE STRING "' in tests_cmake
        and "set(EGTRAIN_CHARACTERIZATION_SCENE_TIMEOUT 300 CACHE STRING" in characterization_cmake
        and "EGTRAIN_TEST_TIMEOUT_SCALE" not in runs(jobs["build"]) + runs(leak) + runs(release["jobs"]["validation"]),
        "release.yml: the sanitizer job scales the test time limits, which only the sanitizer jobs do",
    )


def check_format_and_tidy(checks: Checks, format_workflow: dict, tidy_workflow: dict) -> None:
    """The format and tidy checks install the versions that tools/format.py and tools/tidy.py accept and run the scripts
    that a developer runs. The tidy run analyses the changed sources of a pull request and all sources otherwise."""
    expect = checks.expect
    format_script = (ROOT / "tools/format.py").read_text(encoding="utf-8")
    tidy_script = (ROOT / "tools/tidy.py").read_text(encoding="utf-8")
    format_version = re.search(r'^CLANG_FORMAT_VERSION = "([0-9.]+)"$', format_script, re.MULTILINE)
    format_job = format_workflow["jobs"]["check"]
    expect(format_version and f"pip install 'clang-format=={format_version.group(1)}'" in runs(format_job)
           and "python3 tools/format.py" in runs(format_job),
           "format.yml: the clang-format version of tools/format.py")
    tidy_version = re.search(r'^CLANG_TIDY_VERSION = "(\d+)\.\d+"$', tidy_script, re.MULTILINE)
    tidy_job = tidy_workflow["jobs"]["check"]
    tidy_run = step(tidy_job, "python3 tools/tidy.py")
    expect(
        tidy_version
        and f'sudo "$RUNNER_TEMP/llvm.sh" {tidy_version.group(1)}' in runs(tidy_job)
        and f"sudo apt-get install -y clang-tidy-{tidy_version.group(1)}" in runs(tidy_job)
        and "python3 tools/tidy.py --changed-from HEAD^1" in tidy_run.get("run", "")
        and tidy_run.get("run", "").count("python3 tools/tidy.py") == 2
        and '[ "$GITHUB_EVENT_NAME" = pull_request ]' in tidy_run.get("run", "")
        and tidy_run.get("env", {}).get("CMAKE_PREFIX_PATH") == "${{ env.QT_ROOT_DIR }}"
        and step_index(tidy_job, "./.github/actions/setup-deps") >= 0,
        "tidy.yml: the clang-tidy version of tools/tidy.py, the changed sources of a pull request, all sources otherwise, "
        "with the Qt of the setup action",
    )
    sys.path.insert(0, str(ROOT / "tools"))
    import tidy

    prefix = "EGTRAIN/QEGTRAIN/"
    sources = {prefix + "scene/SceneModel.cpp", prefix + "scene/SceneValidator.cpp", prefix + "util/Log.cpp"}
    expect(
        tidy.select_changed([prefix + "util/Log.cpp", prefix + "simulation/Train.cpp", "README.md"], sources) == [prefix + "util/Log.cpp"]
        and tidy.select_changed([prefix + "scene/SceneModel.h"], sources) == [prefix + "scene/SceneModel.cpp", prefix + "scene/SceneValidator.cpp"]
        and tidy.select_changed([".clang-tidy"], sources) == sorted(sources)
        and tidy.select_changed([prefix + "scene/CMakeLists.txt"], sources) == sorted(sources)
        and tidy.select_changed([prefix + "app/MainWindow.ui"], sources) == sorted(sources)
        and tidy.select_changed([prefix + "simulation/RuntimeLimits.h"], sources) == [prefix + "scene/SceneValidator.cpp"]
        and not tidy.select_changed(["docs/development/build.md"], sources),
        "tools/tidy.py selects the changed sources, the sources that include a changed header, and all for a settings change",
    )
    found = tidy.FINDING.match("/work/EGTRAIN/QEGTRAIN/util/Log.cpp:12:5: warning: message text [bugprone-foo]")
    expect(
        found and (found["line"], found["column"], found["check"]) == ("12", "5", "bugprone-foo")
        and not tidy.FINDING.match("Log.cpp:12:5: warning: message text [bugprone-foo]")
        and not tidy.FINDING.match("/work/Log.cpp:12:5: note: message text")
        and tidy.analysed(prefix + "util/Log.cpp")
        and not tidy.analysed(prefix + "simulation/Train.cpp")
        and not tidy.analysed(prefix + "io/third_party/zmq.hpp")
        and not tidy.analysed("tools/tidy.py"),
        "tools/tidy.py reads the clang-tidy findings and leaves out simulation/ and vendored code",
    )


def check_release(checks: Checks, release: dict, release_text: str, package_check: dict, rules: dict, sources: dict) -> None:
    """The release workflow selects one version, validates and packages it, and publishes only for a push to production
    or a v* tag, after every other job succeeded. The package check runs the same version job and release assets script."""
    expect = checks.expect
    events = triggers(release)
    documentation = sorted(pattern.replace("*", "**") for pattern in rules["cmake"][2])
    expect(
        sorted(events) == ["pull_request", "push", "workflow_dispatch"]
        and events["push"].get("branches") == ["production"] and events["push"].get("tags") == ["v*"]
        and events["pull_request"].get("branches") == ["production"]
        and all(sorted(events[event].get("paths-ignore", [])) == documentation and "paths" not in events[event] for event in ("push", "pull_request")),
        "release.yml: a push or pull request to production that changes more than the files of the documentation rule, "
        "a v* tag, and a manual start",
    )
    expect(release.get("concurrency") == {"group": "${{ github.event_name == 'push' && 'egtrain-publish' || github.run_id }}", "cancel-in-progress": "false"},
           "release.yml: publications run one at a time and are never cancelled")
    jobs = release["jobs"]
    publish = jobs.get("release", {})
    expect(sorted(needs(publish)) == sorted(job for job in jobs if job != "release") and publish.get("if") == PUSH_GATE,
           "release.yml: the release job needs every other job and publishes only for a push to production or a v* tag")
    builds = [job for job in jobs.values() if "cmake -S . -B build" in runs(job)]
    expect(all(needs(job) == ["version"] for job_id, job in jobs.items() if job_id not in ("version", "release"))
           and builds and all('cmake -S . -B build "-DEGTRAIN_VERSION=${{ needs.version.outputs.version }}"' in runs(job) for job in builds)
           and jobs["package"].get("with", {}).get("version") == "${{ needs.version.outputs.version }}",
           "release.yml: every build gets the version that the version job selects")
    prepare = step(publish, "softprops/action-gh-release@") or step(publish, "files: release-assets/*")
    for marker in ("fetch-depth: 0", "run: python3 tools/release/version.py",
                   'production_commit="$(git ls-remote origin refs/heads/production | cut -f1)"', 'if [[ "$production_commit" != "$GITHUB_SHA" ]]',
                   'git ls-remote origin "refs/tags/$tag"', "draft: true", 'gh release edit "$RELEASE_TAG" --draft=false --latest="$MAKE_LATEST"',
                   'tag="v${{ needs.version.outputs.version }}"', "prerelease=false", "make_latest=true"):
        expect(marker in flat(release), f"release.yml: safe versioned publication: {marker}")
    expect(prepare.get("with", {}).get("files") == "release-assets/*" and prepare.get("with", {}).get("fail_on_unmatched_files") == "true"
           and 'tag="main-' not in release_text and 'name="EGTRAIN main build' not in release_text,
           "release.yml: the release publishes the files of the release assets folder and fails when none matches")
    # The package check selects the version as the release does and runs the release assets script as the release job does.
    version = package_check["jobs"]["version"]
    expect(
        version.get("steps") and version.get("steps") == jobs["version"].get("steps") and version.get("outputs") == jobs["version"].get("outputs")
        and version.get("if") == "needs.changes.outputs.package == 'true'",
        "package-check.yml: the version job of the release workflow, skipped with the changes job",
    )
    assets = package_check["jobs"]["release-assets"]
    script = "python3 tools/release/build_release_assets.py --version"
    expect(
        sorted(needs(assets)) == ["package", "version"]
        and step(assets, script) == step(publish, script) != {}
        and step(assets, "actions/download-artifact@") == step(publish, "actions/download-artifact@") != {}
        and in_order(assets, "actions/checkout@", script) and in_order(publish, "actions/checkout@", script)
        and not any(word in flat(assets) for word in ("upload-artifact", "permissions", "if: ")),
        "package-check.yml: the release assets job downloads the artifacts and runs the script as the release job does, "
        "and uploads and publishes nothing",
    )
    # One application version: the CMake cache variable, passed to the program, the Windows resource and the packages.
    for marker, source in (
        ('add_compile_definitions(EGTRAIN_APP_VERSION=\\"${PROJECT_VERSION}\\")', "cmake"),
        ('file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/EGTRAIN_VERSION"', "cmake"),
        ("project(EGTRAIN VERSION ${EGTRAIN_VERSION} LANGUAGES", "cmake"),
        ("setApplicationVersion(QStringLiteral(EGTRAIN_APP_VERSION))", "main"),
        ('VALUE "ProductVersion", "@PROJECT_VERSION@\\0"', "resource"),
        ('VERSION="$(tr -d \'\\r\\n\' < build/EGTRAIN_VERSION)"', "package"),
    ):
        expect(marker in sources[source], f"one application version: {marker}")
    expect(re.search(r'set\(EGTRAIN_VERSION "\d+\.\d+\.\d+" CACHE STRING', sources["cmake"]), "a three-component CMake application version")


def check_packages(checks: Checks, package: dict, package_text: str, package_check: dict, build_release_assets, make_dmg) -> None:
    """The jobs of the package workflow: their artifacts, the order of building, checking and uploading, and signing."""
    expect = checks.expect
    jobs = package["jobs"]
    expect(
        triggers(package) == {"workflow_call": {"inputs": {"version": {"required": "true", "type": "string"}},
                                                "secrets": {"SIGNPATH_API_TOKEN": {"required": "false"}}}},
        "package.yml: starts only when called, with the version and an optional signing token",
    )
    # The release script reads one artifact for each package, the Windows payload and the scene bundles. A name with the
    # version in it is uploaded with the version input.
    uploads = [item.get("with", {}) for job in jobs.values() for item in job["steps"] if item.get("uses", "").startswith("actions/upload-artifact@")]
    for _key, template, directory in build_release_assets.PACKAGES:
        file = template.format(version="${{ inputs.version }}")
        expect(any(upload.get("name") == directory and upload.get("path", "").endswith(file) for upload in uploads),
               f"package.yml: the artifact {directory} holds {file}, which the release script reads")
    for directory in (build_release_assets.WINDOWS_PAYLOAD, build_release_assets.SCENES_ARTIFACT):
        expect(any(upload.get("name") == directory for upload in uploads), f"package.yml: the artifact {directory}")
    expect(build_release_assets.PACKAGES[0][1].format(version="X") == make_dmg.dmg_name("X"), "the disk image has the name that the release script reads")
    scenes = re.search(r"\n *scenes=\(\n((?: +\S+\n)+) *\)\n", runs(jobs["package-macos"]))
    expect(scenes and set(scenes.group(1).split()) == set(build_release_assets.SCENES)
           and 'build/scene_tool pack "EGTRAIN/QEGTRAIN/Scenes/$name" "$bundle"' in runs(jobs["package-macos"])
           and 'build/scene_tool validate "$bundle"' in runs(jobs["package-macos"]),
           "package.yml: the macOS job packs and validates the scene bundles that the release script reads")
    # Every package is a packaged build with the version input, holds Qt Network, the licence, the notices and the manual,
    # runs the update tests and checks the update helper, and starts a copy of itself with the start script.
    macos_script = (ROOT / "tools/release/package_macos.sh").read_text(encoding="utf-8")
    for job_id, network in (("package-macos", '"$APP/Contents/Frameworks/QtNetwork.framework"'), ("package-windows", '"Qt6Network.dll"'),
                            ("package-linux", "libQt6Network.so*")):
        text = runs(jobs[job_id]) + (macos_script if job_id == "package-macos" else "")
        expect(
            all(part in text for part in (network, "LICENSE", "THIRD_PARTY_NOTICES.md", "EGTRAIN.qch", "-DEGTRAIN_PACKAGED_BUILD=ON",
                                          '"-DEGTRAIN_VERSION=${{ inputs.version }}"', "'^test_update(helper|preparation)$'",
                                          "egtrain_update_helper", "tools/release/package_start_smoke.py"))
            and "./.github/actions/setup-deps" in flat(jobs[job_id]) and re.search(r'-DCMAKE_PREFIX_PATH="\$(env:)?QT_ROOT_DIR', runs(jobs[job_id])),
            f"package.yml: {job_id} builds a packaged build of the version with the setup action, checks Qt Network, the "
            "licence, the notices, the manual and the update helper, and starts a copy",
        )
        expect("RUNNER_TEMP" in step(jobs[job_id], "tools/release/package_start_smoke.py").get("run", "")
               and step(jobs[job_id], "tools/release/package_start_smoke.py").get("env", {}).get("PYTHONUTF8") == "1"
               and in_order(jobs[job_id], "python-version: 3.12", "tools/release/package_start_smoke.py"),
               f"package.yml: {job_id} starts a copy under RUNNER_TEMP with Python 3.12")
    for word in ("sha256", "shasum", "Get-FileHash", "unzip", "Compress-Archive", "stat -c", "update-manifest"):
        expect(word.lower() not in (package_text + flat(package_check)).lower(),
               f"package.yml: no {word}: the manifest, its hashes and the archive come from the release script")
    for word in ("action-gh-release", "gh release", "gh api", "git push", "git commit"):
        expect(word not in package_text + flat(package_check), f"package.yml and package-check.yml publish nothing: {word}")
    expect("codesign" not in flat(package_check) and "secrets" not in flat(package_check), "package-check.yml signs nothing and reads no credential")

    # macOS: the script fills and trims the app before the one signature and checks it after; the start script runs a copy;
    # the app is notarized before the disk image is built from it; the upload comes last and the keychain goes on every path.
    macos = jobs["package-macos"]
    subcommands = re.search(r"^\t(\w[\w |-]*)\) ;;$", macos_script, re.MULTILINE)
    subcommands = [name.strip() for name in subcommands.group(1).split("|")] if subcommands else []
    changes = [name for name in subcommands if not name.startswith("verify-")]
    verifies = [name for name in subcommands if name.startswith("verify-")]
    calls = [f"tools/release/package_macos.sh {name} build/QEGTRAIN.app" for name in subcommands]
    expect(
        changes and verifies
        and all(runs(macos).count(call) == 1 for call in calls)
        and in_order(macos, *calls[: len(changes)], "tools/release/sign_macos.sh build/QEGTRAIN.app", *calls[len(changes):],
                     "tools/release/package_start_smoke.py", "tools/release/notarize_macos.sh build/QEGTRAIN.app",
                     "tools/release/make_dmg.py", "hdiutil attach -readonly", "actions/upload-artifact@v4\nwith: name: QEGTRAIN-macos-arm64"),
        "package.yml: the macOS job runs each step of package_macos.sh, the changes before the signature and the checks after "
        "it, then the start, the notarization, the disk image, its check and the upload",
    )
    expect("-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0" in runs(macos), "package.yml: the macOS app is built for macOS 26")
    keychain = macos["steps"][-1]
    expect(keychain.get("if") == "${{ always() && env.MACOS_KEYCHAIN != '' }}" and keychain.get("run", "").strip() == 'security delete-keychain "$MACOS_KEYCHAIN"',
           "package.yml: the last macOS step deletes the signing keychain on every path")
    certificate = step(macos, "secrets.MACOS_CERTIFICATE ")
    certificate_run = certificate.get("run", "")
    expect(
        certificate.get("if") == "${{ " + PUSH_GATE + " }}"
        and 0 <= certificate_run.find('if [ -z "$MACOS_CERTIFICATE" ]; then') < certificate_run.find("security create-keychain")
        < certificate_run.find("security find-identity -v -p codesigning") < certificate_run.find('echo "MACOS_SIGNING_IDENTITY=$IDENTITY" >> "$GITHUB_ENV"')
        and "trap 'rm -f \"$certificate\"' EXIT" in certificate_run,
        "package.yml: the certificate import does nothing without a certificate and passes on the identity only after the import",
    )
    expect(
        step(macos, "tools/release/sign_macos.sh").get("env") is None
        and package_text.count("codesign --force") == 1 and "--deep --sign" not in package_text
        and 0 <= runs(macos).find('if [ -z "${MACOS_SIGNING_IDENTITY:-}" ]; then') < runs(macos).find('codesign --force --timestamp --sign "$MACOS_SIGNING_IDENTITY" "$DMG"'),
        "package.yml: the script alone signs the app, and the disk image is signed with its identity or not at all",
    )
    image = make_dmg.dmg_name("${{ inputs.version }}")
    for target in ("build/QEGTRAIN.app", f"build/dmg/{image}"):
        notary = step(macos, f"tools/release/notarize_macos.sh {target}")
        expect(notary.get("if") == "${{ " + PUSH_GATE + " }}" and notary.get("timeout-minutes") and "secrets.MACOS_NOTARY_KEY" in flat(notary),
               f"package.yml: the notarization of {target} for a push to production or a v* tag, with a time limit")
    expect(
        step(macos, "tools/release/make_dmg.py").get("run", "").strip()
        == 'python3 tools/release/make_dmg.py --version "${{ inputs.version }}" --app build/QEGTRAIN.app --output build/dmg --layout',
        "package.yml: the disk image is built by the script from the final app",
    )
    verify_image = step(macos, "hdiutil attach -readonly")
    after_image = "\n".join(flat(item) for item in macos["steps"][step_index(macos, "hdiutil attach -readonly"):])
    expect(
        all(part in verify_image.get("run", "") for part in (
            f'DMG="$PWD/build/dmg/{image}"', "trap 'hdiutil detach \"$MOUNT\" -force >/dev/null 2>&1 || true' EXIT",
            'test "$(readlink "$MOUNT/Applications")" = "/Applications"', 'cmp "$MOUNT/.DS_Store" tools/release/dmg/DS_Store',
            'codesign --verify --deep --strict --verbose=2 "$MOUNT/QEGTRAIN.app"', 'codesign --verify --deep --strict --verbose=2 "$COPY/QEGTRAIN.app"',
            '[ "$version" = "${{ inputs.version }}" ]', '"$COPY/QEGTRAIN.app/Contents/MacOS/QEGTRAIN" --scene', 'grep -q "End of Simulation"',
            "expect_plist CFBundleDocumentTypes:0:LSItemContentTypes:0 com.egtrain.simulator.egscene", "expect_no_plist CFBundleDocumentTypes:1\n",
            "expect_plist UTExportedTypeDeclarations:0:UTTypeConformsTo:0 public.data", "expect_no_plist UTExportedTypeDeclarations:1\n",
            'test -x "$COPY/QEGTRAIN.app/Contents/MacOS/scene_tool"', 'test -x "$COPY/QEGTRAIN.app/Contents/MacOS/egtrain_update_helper"'))
        and "build/QEGTRAIN.app" not in after_image and "zip" not in flat(macos).lower(),
        "package.yml: the disk image check mounts it read-only, detaches it on every path, checks the signatures, the "
        "document type and the programs of the app in it and of a copy, and runs the copy; no later step uses the app",
    )

    # Windows: the job leaves out the OpenGL, Direct3D, DXC, Qt Quick, QML, virtual keyboard and redistributable files,
    # copies the Visual C++ runtime DLLs, checks the imports, reports the size and the signatures, starts a copy and checks
    # HTTPS from it, and then uploads the folder that it checked.
    windows = jobs["package-windows"]
    windows_runs = runs(windows)
    expect(
        all(option in windows_runs for option in ("--no-compiler-runtime", "--no-opengl-sw", "--no-system-d3d-compiler", "--no-system-dxc-compiler", "--no-quick-import"))
        and '@("opengl32sw.dll","d3dcompiler_47.dll","dxcompiler.dll","dxil.dll","Qt6Quick*","Qt6Qml*","*VirtualKeyboard*","vc_redist*")' in windows_runs
        and all(f'if (-not (Test-Path "$dist/{name}")) {{ throw "missing {name}" }}' in windows_runs for name in ("iconengines/qsvgicon.dll", "styles/qmodernwindowsstyle.dll"))
        and '"$env:VCToolsRedistDir/x64/Microsoft.VC14*.CRT"' in windows_runs and "python tools/release/pe_imports.py $dist --missing" in windows_runs
        and not re.search(r"\d+\.\d+\.\d+", step(windows, "VCToolsRedistDir").get("run", ""))
        and "$env:GITHUB_STEP_SUMMARY" in windows_runs and "zip" not in flat(windows).lower(),
        "package.yml: the Windows package without the software OpenGL, Direct3D, DXC, Qt Quick, QML, virtual keyboard and "
        "redistributable files, with the runtime DLLs that it imports and its size in the job summary",
    )
    # The HTTPS probe exits with this code on a rate limit or an outage of GitHub, which does not fail the job.
    probe = (ROOT / "EGTRAIN/QEGTRAIN/tests/test_updatetransport_https.cpp").read_text(encoding="utf-8")
    skip = re.search(r"^constexpr int kExitSkipped = (\d+);$", probe, re.MULTILINE)
    expect(skip and f"$LASTEXITCODE -ne {skip.group(1)}" in windows_runs, "package.yml: the HTTPS probe of the package skips on the code of the probe")
    report = step(windows, "verify_signatures.ps1 -Directory dist/QEGTRAIN -Report")
    expect(report and "if" not in report, "package.yml: every Windows package run reports the code signatures")
    expect(
        in_order(windows, "windeployqt.exe", "pe_imports.py dist/QEGTRAIN", "-Report", "tools/release/package_start_smoke.py",
                 "test_updatetransport_https.exe", "name: QEGTRAIN-windows-x64-payload")
        and '$env:PATH = "$env:SystemRoot\\System32;$env:SystemRoot"' in windows_runs
        and step(windows, "name: QEGTRAIN-windows-x64-payload").get("with", {}).get("include-hidden-files") == "true",
        "package.yml: the Windows job checks, starts and probes HTTPS from a copy before it uploads the payload with its "
        "hidden files",
    )
    # Signing: the program files only, for a push to production or a v* tag when the repository opts in; the signed payload
    # replaces the artifact. The finalize job builds the installer and the archive from that payload when signing succeeded
    # or was skipped, signs nothing and uploads after its checks. The installer is signed only after the program files were.
    sign = jobs["sign-windows"]
    signed_payload = f"upload-artifact@v4\nwith: name: {build_release_assets.WINDOWS_PAYLOAD}\npath: payload"
    expect(
        sign.get("if") == "${{ " + PUSH_GATE + " && vars.WINDOWS_SIGNING == 'signpath' }}" and needs(sign) == ["package-windows"]
        and sign.get("environment") == "windows-signing"
        and runs(sign).count('@("QEGTRAIN.exe", "egtrain_update_helper.exe", "scene_tool.exe")') == 2
        and ".dll" not in runs(sign).lower()
        and in_order(sign, "signpath/github-action-submit-signing-request@", "-Directory payload -Require -ExpectedSubject", signed_payload)
        and step(sign, signed_payload).get("with", {}).get("overwrite") == "true",
        "package.yml: the Windows signing of the three program files of the payload, which replaces the payload artifact",
    )
    expect("-AllowUntrustedRoot" not in package_text and "-NoTimestampCheck" not in package_text,
           "package.yml: the signature checks require a trusted chain and a timestamp")
    finalize = jobs["finalize-windows"]
    compiles = [line for line in runs(finalize).splitlines() if "& $env:ISCC" in line]
    expect(
        sorted(needs(finalize)) == ["package-windows", "sign-windows"]
        and finalize.get("if") == "${{ !cancelled() && needs.package-windows.result == 'success' && "
                                  "(needs.sign-windows.result == 'success' || needs.sign-windows.result == 'skipped') }}"
        and step(finalize, "actions/download-artifact@") == step(sign, "actions/download-artifact@") != {}
        and not any(word in flat(finalize).lower() for word in ("signtool", "authenticode", "signpath", "environment", "secrets.", "copy-item", "move-item", "remove-item"))
        and len(compiles) == 2 and all('"/DPayload=$payload"' in line for line in compiles) and "/DVersion=$newer" in compiles[1]
        and in_order(finalize, "installer_smoke.ps1", "--payload payload --output portable", "tools/release/package_start_smoke.py", "actions/upload-artifact@")
        and step_index(finalize, "actions/upload-artifact@") > step_index(finalize, "installer_smoke.ps1"),
        "package.yml: the finalize job builds the installer, a newer one for the upgrade and the archive from the payload "
        "after a successful or skipped signing, signs nothing, and uploads after the installer smoke and the start of the "
        "portable archive",
    )
    installer = jobs["sign-windows-installer"]
    installer_artifact = next(directory for key, _template, directory in build_release_assets.PACKAGES if key == "windows-x64-installer")
    expect(
        sorted(needs(installer)) == ["finalize-windows", "sign-windows"] and installer.get("if") == "${{ needs.sign-windows.result == 'success' }}"
        and installer.get("environment") == "windows-signing"
        and step(installer, "actions/download-artifact@") == step(finalize, "actions/download-artifact@") != {}
        and in_order(installer, "-Directory payload -Require -ExpectedSubject", "-Directory signed -Name", f"name: {installer_artifact}\npath: signed/")
        and step(installer, f"name: {installer_artifact}\npath: signed/").get("with", {}).get("overwrite") == "true",
        "package.yml: the installer is signed only after the program files, and replaces its artifact after both checks",
    )

    # Linux: the AppImage holds the SVG icon engine, starts from its extraction on a virtual display with the platform plugin
    # that it holds, and is uploaded after it started. The rehearsal job runs on a runner without Qt.
    linux = jobs["package-linux"]
    expect(
        "EXTRA_QT_MODULES" in flat(linux) and "libqsvgicon.so" in runs(linux)
        and {"xvfb", "xauth"} <= set(step(linux, "./.github/actions/setup-deps").get("with", {}).get("apt-packages", "").split())
        and 'xvfb-run -a -s "-screen 0 1920x1080x24" python3 tools/release/package_start_smoke.py "$start/squashfs-root"' in runs(linux)
        and "QT_QPA_PLATFORM" not in flat(linux) and "QT_PLUGIN_PATH" not in flat(linux)
        and in_order(linux, "libqsvgicon.so", "tools/release/package_start_smoke.py", "actions/upload-artifact@"),
        "package.yml: the Linux AppImage with the SVG icon engine, started on a virtual display before the upload",
    )
    rehearse = jobs["rehearse-linux"]
    rehearse_install = re.search(r"apt-get install -y --no-install-recommends((?:.*\\\n)*.*\n)", runs(rehearse))
    rehearse_packages = rehearse_install.group(1).replace("\\", " ").split() if rehearse_install else []
    expect(
        needs(rehearse) == ["package-linux"] and "if" not in rehearse
        and {"xvfb", "xauth", "xdotool", "imagemagick", "dbus-x11", "at-spi2-core", "python3-gi", "gir1.2-atspi-2.0"} <= set(rehearse_packages)
        and not any("qt" in name.lower() for name in rehearse_packages)
        and '"$image/usr/bin/scene_tool" validate "$work/Downloads/Paimpol.egscene"' in runs(rehearse)
        and "dbus-run-session -- /usr/bin/python3 tools/release/linux_rehearsal.py" in runs(rehearse)
        and '--expected-version "${{ inputs.version }}"' in runs(rehearse)
        and "QT_" not in flat(rehearse) and "QEGTRAIN_" not in flat(rehearse)
        and step(rehearse, "actions/upload-artifact@").get("if") == "${{ always() }}"
        and step(rehearse, "actions/upload-artifact@").get("with", {}).get("name") == "linux-rehearsal-evidence"
        and "linux-rehearsal-evidence" not in [directory for _key, _template, directory in build_release_assets.PACKAGES],
        "package.yml: the Linux rehearsal on a runner without Qt, which uploads its evidence on every path and no release asset",
    )
    # The AppImage needs the glibc of the system that builds it, so it is built on an Ubuntu release no newer than the one
    # that rehearses it, both named explicitly rather than ubuntu-latest.
    releases = [re.fullmatch(r"ubuntu-(\d+)\.04", str(job.get("runs-on", ""))) for job in (linux, rehearse)]
    expect(
        all(releases) and int(releases[0].group(1)) <= int(releases[1].group(1)),
        "package.yml: the AppImage built on a named Ubuntu LTS no newer than the one of its rehearsal",
    )


def check_scripts(checks: Checks, tests_cmake: str) -> None:
    """The release scripts that no CTest runs on every platform: the signature check, the installer smoke and the
    notarization. No workflow and no script turns Gatekeeper off."""
    expect = checks.expect
    verify = (ROOT / "tools/release/verify_signatures.ps1").read_text(encoding="utf-8")
    verify_test = (ROOT / "tools/release/test_verify_signatures.ps1").read_text(encoding="utf-8")
    expect(
        all(part in verify for part in (
            "Get-AuthenticodeSignature -LiteralPath $Path", "$sha256 = '2.16.840.1.101.3.4.2.1'", "$accepted = @('Valid')\n",
            "if ($ExpectedSubject -and $subject -ne $ExpectedSubject) { $problems += ",
            "if (-not $NoTimestampCheck -and -not $signature.TimeStamperCertificate) { $problems += ",
            "if ($digest[0] -ne $sha256) { $problems += ", "if ($Require -and $failed -gt 0) { exit 1 }\nexit 0\n"))
        and verify.count("exit 1") == 1,
        "tools/release/verify_signatures.ps1 checks the status, the digest algorithm, the timestamp and the signer, and fails only with -Require",
    )
    expect(
        all(part in verify_test for part in ("New-SelfSignedCertificate -Type CodeSigningCert", "Set-AuthenticodeSignature", "'SHA1'"))
        and not any(part in verify_test for part in ("Export-", ".pfx", "Import-Certificate", "Cert:\\CurrentUser\\Root", "Cert:\\LocalMachine"))
        and "\nif(WIN32)\n\tfind_program(EGTRAIN_POWERSHELL NAMES pwsh powershell)\n" in tests_cmake
        and "-SceneTool $<TARGET_FILE:scene_tool>" in test_registration(tests_cmake, "test_verify_signatures"),
        "tools/release/test_verify_signatures.ps1 signs with a certificate that it never exports, on Windows only",
    )
    smoke = (ROOT / "tools/release/installer_smoke.ps1").read_text(encoding="utf-8")
    expect(
        all(part in smoke for part in ("'/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'", '/DIR=`"$Directory`"', "'/TASKS=desktopicon'",
                                       "Assert-AsInvoker $Installer", "Assert-AsInvoker $UpgradeInstaller", "Invoke-StartSmoke $directory",
                                       "Install-Egtrain $UpgradeInstaller 'upgrade-over'", "Assert-Registered $defaultDirectory $UpgradeVersion",
                                       "Assert-Survives $outside"))
        and not any(part in smoke.lower() for part in ("/currentuser", "/allusers", "sha256", "get-filehash", "program files")),
        "tools/release/installer_smoke.ps1 installs silently per user, starts, upgrades and checks the registration",
    )
    notarize = (ROOT / "tools/release/notarize_macos.sh").read_text(encoding="utf-8")
    positions = [notarize.find(command) for command in ("xcrun notarytool submit", "--wait", "xcrun stapler staple", "xcrun stapler validate", "spctl --assess")]
    expect(
        -1 not in positions and positions == sorted(positions) and not re.search(r"\bset\s+-\w*x", notarize)
        and not [line for line in notarize.splitlines() if re.search(r"\b(echo|printf|fail)\b.*\$\{?MACOS_NOTARY_", line) and "key.p8" not in line],
        "tools/release/notarize_macos.sh submits and waits, staples, validates and assesses, and prints no credential",
    )
    signature = re.search(r"^verify_signature\(\) \{\n.*?^\}\n", (ROOT / "tools/release/package_macos.sh").read_text(encoding="utf-8"), re.MULTILINE | re.DOTALL)
    expect(
        signature and all(part in signature.group(0) for part in (
            "expect '^Authority=Developer ID Application: '", "expect '^TeamIdentifier=[A-Z0-9]{10}$'",
            "expect '^CodeDirectory .*flags=.*runtime'", "expect '^Timestamp='", "expect '^Signature=adhoc$'")),
        "tools/release/package_macos.sh verify-signature requires Developer ID, a team, the hardened runtime and a timestamp "
        "with an identity, and an ad-hoc signature without one",
    )
    bypass = re.compile(r"\bxattr\b(?:\\\n|[^\n;|&])*(?:[\s'\"]-\w*[cd]|com\.apple\.quarantine)|\bspctl\s+--(master|global)-disable")
    expect(
        all(bypass.search(sample) for sample in ("xattr -r -d com.apple.quarantine app", "xattr -dr app", "xattr \\\n  -c app", "spctl --global-disable"))
        and not bypass.search("spctl --assess --type execute app"),
        "the Gatekeeper pattern matches the xattr and spctl bypass commands and not spctl --assess",
    )
    files = [path for folder in (".github/workflows", ".github/actions", "tools/release", "tools/ci") for path in sorted((ROOT / folder).rglob("*"))
             if path.suffix in (".yml", ".sh", ".py", ".ps1")]
    bypassing = [path.relative_to(ROOT).as_posix() for path in files if bypass.search(path.read_text(encoding="utf-8"))]
    expect(not bypassing, "no workflow or release script removes the quarantine attribute or turns Gatekeeper off: " + ", ".join(bypassing))


def check_ctest(checks: Checks, cmake_lists: str, tests_cmake: str) -> None:
    """How CTest runs the tests that the workflows rely on."""
    expect = checks.expect
    expect("COMMAND python3" not in cmake_lists + tests_cmake and "Python3_EXECUTABLE" in tests_cmake, "CTest runs Python with the configured interpreter")
    for text in (cmake_lists, tests_cmake):
        for command in re.finditer(r"COMMAND bash", text):
            guards = re.findall(r"^\s*(if\(.*\)|else\(\)|elseif\(.*\)|endif\(\))\s*$", text[: command.start()], re.MULTILINE)
            expect(guards and guards[-1] in ("if(UNIX)", "if(APPLE)"), "CTest runs bash only on UNIX")
    expect(re.search(r"if\(WIN32\)(?:(?!endif\(\)).)*?pe_image_size\.py(?:(?!endif\(\)).)*?--max-bytes", tests_cmake, re.DOTALL),
           "CTest checks the image size on Windows")
    expect("foreach(egtrain_case 1 2 3 4 5 7)" in tests_cmake and "tools/e2e/headless_smoke.py ${egtrain_case}" in tests_cmake,
           "CTest runs the headless smoke of the six canonical scenes")
    expect(re.search(r"if\(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4\)\s*message\(FATAL_ERROR", cmake_lists), "a 64-bit Windows build only")
    # A packaged build registers the tests above the line that ends the file without EGTRAIN_BUILD_TESTS.
    packaged = tests_cmake.split("\nif(NOT EGTRAIN_BUILD_TESTS)\n\treturn()\nendif()\n", 1)[0]
    expect(
        packaged != tests_cmake
        and re.search(r"^if\(EGTRAIN_BUILD_TESTS OR EGTRAIN_PACKAGED_BUILD\)\n.*?include\(\$\{SRC_DIR\}/tests/CMakeLists\.txt\)", cmake_lists, re.MULTILINE | re.DOTALL)
        and test_registration(packaged, "test_updatepreparation") and "add_executable(test_updatetransport_https " in packaged,
        "a packaged build registers the update preparation test and builds the HTTPS probe",
    )
    # The HTTPS probe needs internet access, so it is registered only on request and labelled network; the code for a
    # rate limit or an outage of GitHub skips it.
    probe = (ROOT / "EGTRAIN/QEGTRAIN/tests/test_updatetransport_https.cpp").read_text(encoding="utf-8")
    skip = re.search(r"^constexpr int kExitSkipped = (\d+);$", probe, re.MULTILINE)
    registered = re.search(r"\nif\(EGTRAIN_NETWORK_TESTS\)\n\tegtrain_add_test\(test_updatetransport_https LABELS integration network TIMEOUT \d+ SKIP_RETURN_CODE (\d+)\)\nendif\(\)\n", tests_cmake)
    expect(
        'option(EGTRAIN_NETWORK_TESTS "Register the tests that need internet access" OFF)\n' in cmake_lists
        and skip and registered and skip.group(1) == registered.group(1)
        and re.search(r"\negtrain_add_test\(test_updatetransport_https_cases\n\tCOMMAND \$\{Python3_EXECUTABLE\}\n[^\n]*test_updatetransport_https_cases\.py \$<TARGET_FILE:test_updatetransport_https>\n", tests_cmake),
        "the HTTPS probe registered only with EGTRAIN_NETWORK_TESTS, skipped on the code that it returns, and its answers tested everywhere",
    )
    # A shell smoke writes below TMPDIR only into folders that mktemp makes, named qegtrain-<smoke>-e2e.<random> when it
    # keeps them for the failure upload.
    for script in sorted((ROOT / "tools/e2e").glob("*.sh")):
        paths = re.findall(r'(mktemp -d )?"\$\{TMPDIR:-/tmp\}/([^"]*)"', script.read_text(encoding="utf-8"))
        expect(all(made and name.endswith(".XXXXXX") for made, name in paths), f"{script.name} writes below TMPDIR only into mktemp folders")
    for smoke in ("csv_export", "editor", "legacy_import", "scene_render", "track_preview", "visual_polish"):
        text = (ROOT / f"tools/e2e/{smoke}_smoke.sh").read_text(encoding="utf-8")
        expect(re.search(r'mktemp -d "\$\{TMPDIR:-/tmp\}/qegtrain-[a-z-]+-e2e\.XXXXXX"', text), f"{smoke}_smoke.sh keeps its logs in a qegtrain-*-e2e folder")


def main() -> None:
    # The release scripts name the artifacts, the scene bundles and the disk image that the workflows must agree with.
    # Other tests import this file, so these imports are here and not at module level.
    sys.path.insert(0, str(ROOT / "tools/release"))
    sys.path.insert(0, str(ROOT / "tools/ci"))
    import build_release_assets
    import changed_files
    import make_dmg

    paths = sorted((ROOT / ".github/workflows").glob("*.yml"))
    texts = {path.name: path.read_text(encoding="utf-8") for path in paths}
    workflows = {name: load_yaml(text) for name, text in texts.items()}
    action_text = (ROOT / ".github/actions/setup-deps/action.yml").read_text(encoding="utf-8")
    action = load_yaml(action_text)
    cmake_lists = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    tests_cmake = (ROOT / "EGTRAIN/QEGTRAIN/tests/CMakeLists.txt").read_text(encoding="utf-8")
    characterization_cmake = (ROOT / "EGTRAIN/QEGTRAIN/tests/characterization/CMakeLists.txt").read_text(encoding="utf-8")
    presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))["configurePresets"]
    release_text = release_pipeline_text()
    sources = {
        "cmake": cmake_lists,
        "main": (ROOT / "EGTRAIN/QEGTRAIN/app/main.cpp").read_text(encoding="utf-8"),
        "resource": (ROOT / "EGTRAIN/QEGTRAIN/resources/app/egtrain.rc.in").read_text(encoding="utf-8"),
        "package": texts["package.yml"],
    }

    checks = Checks()
    # Where PyYAML is installed, the reader reads every file as it does.
    try:
        import yaml
    except ImportError:
        yaml = None
    if yaml:
        for name, text in {**texts, "setup-deps": action_text}.items():
            checks.expect(load_yaml(text) == yaml.load(text, Loader=yaml.BaseLoader), f"{name}: read as PyYAML reads it")

    check_reader(checks)
    check_changed_file_names(checks, changed_files)
    check_required_workflows(checks, workflows, changed_files.RULES)
    check_every_workflow(checks, workflows)
    check_credentials(checks, workflows)
    check_reusable_calls(checks, workflows)
    check_references(checks, {**texts, "setup-deps": action_text}, tests_cmake, characterization_cmake)
    check_dependency_setup(checks, workflows, texts, action)
    check_build(checks, workflows["cmake.yml"], texts["cmake.yml"], presets, workflows["package.yml"], release_text, cmake_lists)
    check_sanitizers(checks, workflows["cmake.yml"], workflows["release.yml"], tests_cmake, characterization_cmake)
    check_format_and_tidy(checks, workflows["format.yml"], workflows["tidy.yml"])
    check_release(checks, workflows["release.yml"], release_text, workflows["package-check.yml"], changed_files.RULES, sources)
    check_packages(checks, workflows["package.yml"], texts["package.yml"], workflows["package-check.yml"], build_release_assets, make_dmg)
    check_scripts(checks, tests_cmake)
    check_ctest(checks, cmake_lists, tests_cmake)
    checks.finish()


if __name__ == "__main__":
    main()
