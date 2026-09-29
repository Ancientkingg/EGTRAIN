#!/usr/bin/env python3
"""Offscreen dialog contract using the existing creator-acceptance scene."""

import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parents[2]


def markers(output: str, prefix: str) -> dict:
    return dict(line.split("=", 1) for line in output.splitlines() if line.startswith(prefix) and "=" in line)


def launch(app: Path, settings: Path, scene: Optional[Path] = None) -> dict:
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM="offscreen", QEGTRAIN_E2E_SETTINGS_DIR=str(settings))
    if scene:
        env.update(QEGTRAIN_AUTOSTART="1", QEGTRAIN_E2E_REVIEW_CANCEL="1")
        args = [str(app), "--scene", str(scene)]
        prefix = "E2E_RUN_REVIEW_"
    else:
        env["QEGTRAIN_E2E_STARTUP_CHOOSER"] = "1"
        args = [str(app)]
        prefix = "E2E_STARTUP_CHOOSER_"
    result = subprocess.run(args, cwd=ROOT / "EGTRAIN/QEGTRAIN", env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, timeout=90, check=False)
    if result.returncode:
        raise AssertionError(f"dialog subprocess exited {result.returncode}: {result.stdout[-3000:]}")
    values = markers(result.stdout, prefix)
    if not values:
        raise AssertionError(f"missing {prefix} markers: {result.stdout[-3000:]}")
    return values


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: dialog_presentation_contract.py APP CREATOR_ACCEPTANCE_SCENE")
    app, scene = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="egtrain-dialog-contract-") as temporary:
        settings = Path(temporary)
        chooser = launch(app, settings)
        assert chooser["E2E_STARTUP_CHOOSER_GROUP"] == "Bundled cases", chooser
        assert chooser["E2E_STARTUP_CHOOSER_GROUP_SELECTABLE"] == "no", chooser
        assert chooser["E2E_STARTUP_CHOOSER_FIRST"] == "Assignment Gvc-Gdg-Ut", chooser
        assert chooser["E2E_STARTUP_CHOOSER_OPEN_ENABLED"] == "yes", chooser
        assert chooser["E2E_STARTUP_CHOOSER_ACTION"] == "Continue", chooser
        assert chooser["E2E_STARTUP_CHOOSER_UNCHANGED"] == "yes", chooser
        for advanced in (False, True):
            ini = settings / "EGTRAIN/EGTRAIN.ini"
            ini.parent.mkdir(parents=True, exist_ok=True)
            ini.write_text(f"[General]\nadvancedDetails={'true' if advanced else 'false'}\n")
            review = launch(app, settings, scene)
            assert review["E2E_RUN_REVIEW_SUMMARY"] == "3", review
            assert review["E2E_RUN_REVIEW_START"] == "09:15:30", review
            assert review["E2E_RUN_REVIEW_DURATION"] == "900 s elapsed (ends near 09:30:30)", review
            assert review["E2E_RUN_REVIEW_DETAILS"] == "1", review
            assert review["E2E_RUN_REVIEW_ADVANCED"] == str(int(advanced)), review
            assert review["E2E_RUN_REVIEW_VALIDATION_VISIBLE"] == str(int(advanced)), review
            assert review["E2E_RUN_REVIEW_CANCELLED"] == "1", review
            assert review["E2E_RUN_REVIEW_UNCHANGED"] == "1", review
    print("chooser and normal/advanced run-review Cancel contract passed")


if __name__ == "__main__":
    main()
