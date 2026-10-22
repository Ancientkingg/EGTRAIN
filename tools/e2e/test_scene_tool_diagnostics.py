#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


def run_tool(scene_tool: Path, *args: object) -> subprocess.CompletedProcess:
    return subprocess.run(
        [scene_tool, *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


# A scene of another version or layout fails with one line that names the reason, not with the loader's errors.
def expect_rejection(scene_tool: Path, args: tuple, *details: str) -> None:
    proc = run_tool(scene_tool, *args)
    label = " ".join(str(arg) for arg in args)
    if proc.returncode != 1:
        raise SystemExit(f"scene_tool {label} exited with {proc.returncode}, expected 1:\n{proc.stdout}")
    if len(proc.stdout.splitlines()) != 1:
        raise SystemExit(f"scene_tool {label} printed more than the reason:\n{proc.stdout}")
    for detail in details:
        if detail not in proc.stdout:
            raise SystemExit(f"scene_tool {label} lost {detail!r}:\n{proc.stdout}")


def check_version_rejections(scene_tool: Path, root: Path) -> None:
    draft = root / "EGTRAIN/QEGTRAIN/tests/fixtures/scene-versions/draft-layout-Paimpol"
    with tempfile.TemporaryDirectory(prefix="scene-tool-versions-") as temp_name:
        temp = Path(temp_name)
        for name, schema_version, saved_with in (("newer", 2, "9.9"), ("older", 0, None)):
            scene = temp / name
            scene.mkdir()
            manifest = {"name": name, "schema_version": schema_version}
            if saved_with:
                manifest["saved_with_app_version"] = saved_with
            (scene / "scene.json").write_text(json.dumps(manifest), encoding="utf-8")
        bundle = temp / "newer.egscene"
        with zipfile.ZipFile(bundle, "w") as archive:
            archive.writestr("scene.json", json.dumps({"format": "egscene", "bundle_version": 2, "schema_version": 1}))
            archive.writestr("future-layout.json", "future")
        packed = temp / "draft.egscene"

        expect_rejection(scene_tool, ("validate", draft),
            "scene.compatibility.draft", "draft layout", "2026-08-08", "Load Legacy Case")
        expect_rejection(scene_tool, ("pack", draft, packed), "scene.compatibility.draft", "Load Legacy Case")
        if packed.exists():
            raise SystemExit("scene_tool pack wrote a bundle of a draft scene")
        expect_rejection(scene_tool, ("validate", temp / "newer"),
            "scene.compatibility.newer", "schema 2 (saved with EGTRAIN 9.9)", "reads schema 1", "Check for Updates")
        expect_rejection(scene_tool, ("validate", temp / "older"),
            "scene.compatibility.older", "schema 0 (saved-with version not recorded)", "reads schema 1")
        expect_rejection(scene_tool, ("validate", bundle),
            "scene.compatibility.newer", "schema 1, bundle layout 2", "reads schema 1, bundle layout 1")


# A path that the file system refuses ends with a diagnostic and exit code 1, not with an abort.
def check_unreadable_paths(scene_tool: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="scene-tool-unreadable-") as temp_name:
        temp = Path(temp_name)
        proc = run_tool(scene_tool, "validate", temp / ("x" * 300) / "scene")
        if proc.returncode != 1 or "[scene.dir." not in proc.stdout:
            raise SystemExit(f"validate of a 300-character name exited with {proc.returncode}:\n{proc.stdout}")
        # A folder without permissions does not stop root, and Windows does not use the permission bits.
        if os.name == "nt" or os.geteuid() == 0:
            return
        closed = temp / "closed"
        (closed / "case").mkdir(parents=True)
        closed.chmod(0)
        try:
            proc = run_tool(scene_tool, "import", closed / "case", temp / "scene")
        finally:
            closed.chmod(0o700)
        if proc.returncode != 1 or "scene.import.missing" not in proc.stdout:
            raise SystemExit(f"import from a folder that cannot be read exited with {proc.returncode}:\n{proc.stdout}")


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    scene_tool = Path(sys.argv[1])
    proc = subprocess.run(
        [scene_tool, "validate", root / "EGTRAIN/QEGTRAIN/Scenes/Copenhagen"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        raise SystemExit(proc.stdout)

    dwell = [line for line in proc.stdout.splitlines() if "[scene.dwell.exceeds_window]" in line]
    if len(dwell) != 1:
        raise SystemExit(f"expected one grouped dwell diagnostic, got {len(dwell)}")
    if not dwell[0].split("x ", 1)[0].isdigit():
        raise SystemExit(f"expected grouped diagnostic count: {dwell[0]}")
    for detail in ("services.json", "Minimum dwell is longer than the time from arrival to departure"):
        if detail not in dwell[0]:
            raise SystemExit(f"grouped diagnostic lost {detail}: {dwell[0]}")
    for context in (
        "(service A-Hillerod-Hundige) at services[A-Hillerod-Hundige].stops[1].dwell_seconds",
        "(service A-Hillerod-Hundige-2) at services[A-Hillerod-Hundige-2].stops[1].dwell_seconds",
    ):
        if context not in proc.stdout:
            raise SystemExit(f"grouped diagnostic lost context: {context}")

    runnable = subprocess.run(
        [scene_tool, "validate", "--runnable", root / "EGTRAIN/QEGTRAIN/Scenes/Copenhagen"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if runnable.returncode != 0:
        raise SystemExit(runnable.stdout)
    # Only the runnable layer reports a clipped final block.
    if "scene.native.block.clipped" not in runnable.stdout:
        raise SystemExit(f"validate --runnable lost the run checks: {runnable.stdout}")
    if "scene.native.block.clipped" in proc.stdout:
        raise SystemExit("validate printed a run check")

    check_version_rejections(scene_tool, root)
    check_unreadable_paths(scene_tool)


if __name__ == "__main__":
    main()
