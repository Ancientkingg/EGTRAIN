#!/usr/bin/env python3
"""Offscreen real-event chooser contract; startup already loads Netherlands.

Dirty/native-picker and no-loaded startup states require separate manual coverage.
"""

import html
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def tooltip(name, path):
    """The tooltip of a row: Qt::convertFromPlainText of the name, a line break and the path."""
    return f"<p>{html.escape(name, quote=False)}<br>\n{html.escape(str(path), quote=False)}</p>"


def shown_lines(tip):
    """The lines a tooltip shows once the rich text is read: the name and the path as typed."""
    assert tip.startswith("<p>") and tip.endswith("</p>"), tip
    return [html.unescape(line) for line in tip[3:-4].split("<br>\n")]


def launch(app: Path, settings: Path, recents=(), interaction="", target=None):
    ini = settings / "EGTRAIN/EGTRAIN.ini"
    ini.parent.mkdir(parents=True)
    ini.write_text("[General]\n" + ("recentScenes=" + ", ".join(map(str, recents)) + "\n"
                                       if recents else ""))
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM="offscreen", QEGTRAIN_E2E_SETTINGS_DIR=str(settings),
               QEGTRAIN_E2E_STARTUP_CHOOSER="1",
               QEGTRAIN_E2E_CHOOSER_INTERACTION=interaction,
               QEGTRAIN_E2E_CHOOSER_PATH=str(target) if target else "")
    result = subprocess.run([str(app)], cwd=ROOT / "EGTRAIN/QEGTRAIN", env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, timeout=90, check=False)
    assert result.returncode == 0, result.stdout[-3000:]
    prefix = "E2E_STARTUP_CHOOSER_CONTRACT="
    lines = [line[len(prefix):] for line in result.stdout.splitlines() if line.startswith(prefix)]
    assert len(lines) == 1, result.stdout[-3000:]
    contract = json.loads(lines[0])
    # A tooltip carries zero-width spaces where a long run of characters may wrap.
    for row in contract["rows"]:
        row["tooltip"] = row["tooltip"].replace("\u200b", "")
    return contract, ini.read_text()


def check_common(contract):
    assert not contract["timedOut"], contract
    rows = contract["rows"]
    assert rows[0]["title"] == "Bundled cases", rows
    for row in rows:
        if not row["enabled"]:
            assert not row["selectable"], row
    assert contract["defaultOpen"] and contract["openEnabled"], contract
    assert contract["loadedBefore"], contract  # Not a no-loaded startup test.
    assert contract["otherActions"] == ["New Case Study...", "Open Scene Folder...",
                                         "Import Legacy Case..."], contract


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: case_chooser_contract.py APP SCENE")
    app, scene = (Path(arg).resolve() for arg in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="egtrain-case-chooser-") as temporary:
        root = Path(temporary)
        empty, _ = launch(app, root / "empty-settings")
        check_common(empty)
        # Without a recent case the list has no Recent cases group and no hint row; it ends with the last bundled case.
        assert not any(row["title"] in ("Recent cases", "No other recent cases to show")
                       for row in empty["rows"]), empty
        assert empty["rows"][-1]["enabled"] and empty["rows"][-1]["selectable"], empty
        assert empty["chosenAction"] == "ContinueCurrent" and not empty["accepted"], empty
        assert empty["loadedBefore"] == empty["loadedAfter"], empty
        first = next(row for row in empty["rows"] if row["enabled"])
        assert empty["initialPath"] == first["path"], empty

        fixture = root / "human readable & exact stored path"
        shutil.copytree(scene, fixture)
        manifest_path = fixture / "scene.json"
        manifest = json.loads(manifest_path.read_text())
        title = "Regional passenger case <East & West> " + "with a long human-readable title " * 8
        manifest["name"] = title.strip()
        manifest_path.write_text(json.dumps(manifest))
        alias = root / "same-scene-alias"
        alias.symlink_to(fixture, target_is_directory=True)
        missing = root / "missing_case"
        recents = [fixture, alias, Path(first["path"]), missing, f"{missing}/"]

        for interaction in ("select", "open", "keyboard", "doubleclick"):
            contract, settings_text = launch(app, root / f"settings-{interaction}", recents,
                                             interaction, fixture)
            check_common(contract)
            rows = contract["rows"]
            fixture_rows = [row for row in rows if row["path"] == str(fixture)]
            assert len(fixture_rows) == 1, rows
            assert fixture_rows[0] == {"title": title.strip(), "path": str(fixture),
                                       "enabled": True, "selectable": True,
                                       "tooltip": tooltip(title.strip(), fixture)}, fixture_rows
            assert shown_lines(fixture_rows[0]["tooltip"]) == [title.strip(), str(fixture)], fixture_rows
            assert not any(row["path"] == str(alias) for row in rows), rows
            assert sum(row["path"] == first["path"] for row in rows) == 1, rows
            assert sum(row["title"] == "Recent cases" for row in rows) == 1, rows
            assert not any(row["title"] == "No other recent cases to show" for row in rows), rows
            unavailable = next(row for row in rows if row["tooltip"] == tooltip("missing case", missing))
            assert shown_lines(unavailable["tooltip"]) == ["missing case", str(missing)], unavailable
            assert unavailable["title"] == "Unavailable: missing case", unavailable
            assert sum(row["title"].startswith("Unavailable: ") for row in rows) == 1, rows
            assert unavailable["enabled"] and unavailable["selectable"] and not unavailable["path"], unavailable
            assert str(missing) in settings_text, settings_text
            menu_missing = next(action for action in contract["recentActions"]
                                if action["path"] == str(missing))
            assert menu_missing["title"].startswith("Unavailable: "), menu_missing
            assert sum(action["title"].startswith("Unavailable: ") for action in contract["recentActions"]) == 1, contract
            assert not menu_missing["enabled"] and menu_missing["tooltip"] == str(missing), menu_missing
            assert contract["selectedPath"] == str(fixture), contract
            if interaction == "select":
                assert contract["chosenAction"] == "ContinueCurrent" and not contract["accepted"], contract
                assert contract["loadedAfter"] == contract["loadedBefore"], contract
            else:
                assert contract["accepted"] and contract["chosenAction"] == "OpenSelected", contract
                assert contract["chosenPath"] == str(fixture), contract
                assert contract["loaded"] and contract["loadedAfter"] == str(fixture), contract
            print(f"{interaction}: rows, retained unavailable recent, deduplication and exact path passed")
    print("case chooser contract passed (loaded startup; dirty/native picker remain manual)")


if __name__ == "__main__":
    main()
