#!/usr/bin/env python3
"""Checks the Markdown documentation of the repository.

  python3 tools/docs/check_docs.py              check the repository that holds this script
  python3 tools/docs/check_docs.py --root DIR   check the Git repository in DIR

The files are the ones Git lists: tracked files and new files that are not ignored, so a new
document is checked before it is staged. The Markdown files among them (README.md, docs/ and
.github/ISSUE_TEMPLATE/) are read; the other files matter only as link targets and as images.
The kinds of finding are:

  link          a relative link or image whose file or directory is not in the repository
  anchor        a link to a heading that the target document does not have
  image-alt     an image with empty alt text
  orphan-image  an image below docs/ that no document links to
  unreachable   a document below docs/ that docs/README.md does not reach through links
  shell         a bash or sh example that does not split into shell words
  json          a json example that is not valid JSON
  dash          a line with an em dash or an en dash
  local-path    a line with an absolute path of a user's machine

Each finding is printed as path:line: [kind] message, followed by a summary line. The exit code is
0 when nothing is found, 1 when there are findings and 2 when the check cannot run (Git fails, or
a document is not valid UTF-8). A bash or sh example that contains a here-document is not checked.

Only inline links [text](target) and images ![alt](target) are read, outside fenced code blocks and
inline code spans. Reference-style links, autolinks, HTML and setext headings are not read. Links
with a scheme (https:, mailto:) are never fetched. Nothing is rendered.
"""
import argparse
import json
import posixpath
import re
import shlex
import subprocess
import sys
from collections import namedtuple
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
LANDING_PAGE = "docs/README.md"
IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp")
SHELL_INFO_WORDS = ("bash", "sh")
EM_DASH = "\u2014"
EN_DASH = "\u2013"

FENCE_OPEN = re.compile(r"^ *(`{3,}(?=[^`]*$)|~{3,})[ \t]*(\S*)")
FENCE_CLOSE = re.compile(r"^ *(`{3,}|~{3,})[ \t]*$")
HEADING = re.compile(r"^ {0,3}#{1,6} +(.*)$")
CLOSING_HASHES = re.compile(r"(^|[ \t]+)#+[ \t]*$")
HEADING_LINK = re.compile(r"\[([^\]]*)\]\([^)]*\)")
CODE_SPAN = re.compile(r"(?<!`)(`+)(?!`).+?(?<!`)\1(?!`)")
LINK_TITLE = r"""(?:\s+(?:"[^"]*"|'[^']*'|\([^)]*\)))?"""
LINK = re.compile(r"(!?)\[((?:[^\[\]]|\[[^\[\]]*\])*)\]\(\s*(<[^>]*>|[^\s)]*)" + LINK_TITLE + r"\s*\)")
SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
LOCAL_PATH = re.compile(r"/Users/|/home/[A-Za-z]|[A-Za-z]:[\\/]Users[\\/]")

Finding = namedtuple("Finding", "path line kind message")
Document = namedtuple("Document", "lines anchors links blocks")
Link = namedtuple("Link", "line image text target")
Block = namedtuple("Block", "line info body")
Reference = namedtuple("Reference", "path link target fragment")


class ToolError(Exception):
    """A failure of the check itself, as opposed to a finding in the documents."""


def format_finding(finding) -> str:
    return f"{finding.path}:{finding.line}: [{finding.kind}] {finding.message}"


def tracked_files(root) -> list:
    command = ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    try:
        result = subprocess.run(command, capture_output=True)
    except OSError as error:
        raise ToolError(f"cannot run {' '.join(command)}: {error}")
    if result.returncode != 0:
        lines = result.stderr.decode("utf-8", "replace").strip().splitlines()
        reason = lines[0] if lines else f"exit status {result.returncode}"
        raise ToolError(f"{' '.join(command)} failed: {reason}")
    names = result.stdout.decode("utf-8", "replace").split("\0")
    return sorted({name for name in names if name and (Path(root) / name).is_file()})


def anchor_of(heading: str) -> str:
    text = HEADING_LINK.sub(r"\1", heading)
    text = text.replace("`", "").replace("*", "").strip().lower()
    return re.sub(r"[^\w\- ]", "", text).replace(" ", "-")


def find_links(text: str, first_line: int):
    """Yields the links of a paragraph, including the ones in the text of a link, with the line each starts on."""
    for match in LINK.finditer(text):
        image, label, target = match.groups()
        if target.startswith("<") and target.endswith(">"):
            target = target[1:-1]
        yield Link(first_line + text.count("\n", 0, match.start()), image == "!", label, target)
        yield from find_links(label, first_line + text.count("\n", 0, match.start(2)))


def parse(text: str) -> Document:
    lines = [line.rstrip("\r") for line in text.split("\n")]
    anchors, blocks, counts = set(), [], {}
    paragraphs, paragraph = [], []  # runs of unfenced, non-blank lines as (number, line)
    fence = None  # marker, line, info word and body lines of the open fenced block
    for number, line in enumerate(lines, 1):
        if fence:
            marker, start, info, body = fence
            closing = FENCE_CLOSE.match(line)
            if closing and closing.group(1)[0] == marker[0] and len(closing.group(1)) >= len(marker):
                blocks.append(Block(start, info, body))
                fence = None
            else:
                body.append(line)
            continue
        opening = FENCE_OPEN.match(line)
        if opening or not line.strip():
            paragraphs.append(paragraph)
            paragraph = []
            if opening:
                fence = (opening.group(1), number, opening.group(2), [])
            continue
        heading = HEADING.match(line)
        if heading:
            title = CLOSING_HASHES.sub("", heading.group(1)).strip()
            if title:
                base = anchor_of(title)
                repeats = counts.get(base, 0)
                counts[base] = repeats + 1
                anchors.add(base if repeats == 0 else f"{base}-{repeats}")
        paragraph.append((number, line))
    paragraphs.append(paragraph)
    if fence:
        blocks.append(Block(fence[1], fence[2], fence[3]))
    links = []
    for numbered_lines in paragraphs:
        if numbered_lines:
            prose = CODE_SPAN.sub("_", "\n".join(line for _, line in numbered_lines))
            links.extend(find_links(prose, numbered_lines[0][0]))
    return Document(lines, anchors, links, blocks)


def read_document(root, path: str) -> Document:
    try:
        data = (Path(root) / path).read_bytes()
    except OSError as error:
        raise ToolError(f"cannot read {path}: {error}")
    try:
        return parse(data.decode("utf-8"))
    except UnicodeDecodeError:
        raise ToolError(f"{path} is not valid UTF-8")


def references(documents: dict) -> list:
    """Lists the links that stay in the repository, with the path and the fragment they name."""
    found = []
    for path, document in documents.items():
        for link in document.links:
            if SCHEME.match(link.target):
                continue
            location, _, fragment = link.target.partition("#")
            target = path
            if location:
                target = posixpath.normpath(posixpath.join(posixpath.dirname(path), unquote(location)))
            found.append(Reference(path, link, target, unquote(fragment)))
    return found


def check_links(refs: list, listed: set, directories: set) -> list:
    findings = []
    for path, link, target, _ in refs:
        slash = link.target.partition("#")[0].endswith("/")  # a trailing slash names a directory, not a file
        if not link.target:
            findings.append(Finding(path, link.line, "link", "empty link target"))
        elif target not in directories and (slash or target not in listed):
            findings.append(Finding(path, link.line, "link", f"no such file or directory: {link.target}"))
    return findings


def check_anchors(refs: list, documents: dict) -> list:
    return [
        Finding(path, link.line, "anchor", f"no heading with the anchor #{fragment} in {target}")
        for path, link, target, fragment in refs
        if fragment and target in documents and fragment not in documents[target].anchors
    ]


def check_image_alt(documents: dict) -> list:
    return [
        Finding(path, link.line, "image-alt", f"image has no alt text: {link.target}")
        for path, document in documents.items()
        for link in document.links
        if link.image and not link.text.strip()
    ]


def check_orphan_images(refs: list, listed: set) -> list:
    referenced = {target for _, _, target, _ in refs}
    return [
        Finding(path, 1, "orphan-image", "no document links to this image")
        for path in sorted(listed)
        if path.startswith("docs/") and path.lower().endswith(IMAGE_SUFFIXES) and path not in referenced
    ]


def check_reachability(refs: list, documents: dict) -> list:
    if LANDING_PAGE not in documents:
        return [Finding(LANDING_PAGE, 1, "unreachable", "landing page missing")]
    edges = {}
    for path, _, target, _ in refs:
        if target in documents:
            edges.setdefault(path, set()).add(target)
    reached = {LANDING_PAGE}
    pending = [LANDING_PAGE]
    while pending:
        for target in edges.get(pending.pop(), ()):
            if target not in reached:
                reached.add(target)
                pending.append(target)
    return [
        Finding(path, 1, "unreachable", f"not reachable from {LANDING_PAGE}")
        for path in documents
        if path.startswith("docs/") and path not in reached
    ]


def check_shell(documents: dict) -> list:
    findings = []
    for path, document in documents.items():
        for block in document.blocks:
            text = "\n".join(block.body)
            if block.info not in SHELL_INFO_WORDS or "<<" in text:
                continue
            try:
                shlex.split(text, comments=True)
            except ValueError as error:
                findings.append(Finding(path, block.line, "shell", str(error)))
    return findings


def check_json(documents: dict) -> list:
    findings = []
    for path, document in documents.items():
        for block in document.blocks:
            if block.info != "json":
                continue
            try:
                json.loads("\n".join(block.body))
            except ValueError as error:
                findings.append(Finding(path, block.line, "json", str(error)))
    return findings


def check_dashes(documents: dict) -> list:
    findings = []
    for path, document in documents.items():
        for number, line in enumerate(document.lines, 1):
            names = [name for dash, name in ((EM_DASH, "em dash"), (EN_DASH, "en dash")) if dash in line]
            if names:
                findings.append(Finding(path, number, "dash", " and ".join(names)))
    return findings


def check_local_paths(documents: dict) -> list:
    return [
        Finding(path, number, "local-path", "absolute path of a local machine")
        for path, document in documents.items()
        for number, line in enumerate(document.lines, 1)
        if LOCAL_PATH.search(line)
    ]


def check(root, files) -> list:
    """Returns the findings for the listed files, relative to root, sorted by path, line, kind and message."""
    listed = set(files)
    directories = {"."}
    for path in listed:
        parent = posixpath.dirname(path)
        while parent:
            directories.add(parent)
            parent = posixpath.dirname(parent)
    documents = {path: read_document(root, path) for path in sorted(listed) if path.endswith(".md")}
    refs = references(documents)
    findings = []
    findings += check_links(refs, listed, directories)
    findings += check_anchors(refs, documents)
    findings += check_image_alt(documents)
    findings += check_orphan_images(refs, listed)
    findings += check_reachability(refs, documents)
    findings += check_shell(documents)
    findings += check_json(documents)
    findings += check_dashes(documents)
    findings += check_local_paths(documents)
    return sorted(findings)


def main(argv=None) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")  # a path that the console cannot encode must not end the run
    parser = argparse.ArgumentParser(description="Check the Markdown documentation of the repository.")
    parser.add_argument(
        "--root", type=Path, default=ROOT, help="the Git repository to check (default: the one that holds this script)"
    )
    arguments = parser.parse_args(argv)
    try:
        findings = check(arguments.root, tracked_files(arguments.root))
    except ToolError as error:
        print(f"check_docs: {error}", file=sys.stderr)
        return 2
    for finding in findings:
        print(format_finding(finding))
    if not findings:
        print("no findings")
    else:
        print(f"{len(findings)} finding" + ("" if len(findings) == 1 else "s"))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
