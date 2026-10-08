#!/usr/bin/env python3
"""Offscreen dialog contract using the existing creator-acceptance scene."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timedelta
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parents[2]


def markers(output: str, prefix: str) -> dict:
    return dict(line.split("=", 1) for line in output.splitlines() if line.startswith(prefix) and "=" in line)


def launch(app: Path, settings: Path, scene: Optional[Path] = None,
           cancellation: str = "button", enlarged: bool = False) -> dict:
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM="offscreen", QEGTRAIN_E2E_SETTINGS_DIR=str(settings))
    if scene:
        env.update(QEGTRAIN_AUTOSTART="1", QEGTRAIN_E2E_REVIEW_CANCEL=cancellation)
        if enlarged:
            env["QEGTRAIN_E2E_REVIEW_ENLARGED"] = "1"
        else:
            env.pop("QEGTRAIN_E2E_REVIEW_ENLARGED", None)
        args = [str(app), "--scene", str(scene)]
        prefix = "E2E_RUN_REVIEW_"
    else:
        env["QEGTRAIN_E2E_STARTUP_CHOOSER"] = "1"
        args = [str(app)]
        prefix = "E2E_STARTUP_CHOOSER_"
    try:
        result = subprocess.run(args, cwd=ROOT / "EGTRAIN/QEGTRAIN", env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=90, check=False)
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        raise AssertionError(f"dialog subprocess timed out: {output[-3000:]}") from error
    if result.returncode:
        raise AssertionError(f"dialog subprocess exited {result.returncode}: {result.stdout[-3000:]}")
    values = markers(result.stdout, prefix)
    if not values:
        raise AssertionError(f"missing {prefix} markers: {result.stdout[-3000:]}")
    return values


def expected_review(scene: Path) -> tuple:
    """Fixture-derived expectations: the excess dwell warning and the missing signalling level warning."""
    manifest = json.loads((scene / "scene.json").read_text())
    services = json.loads((scene / "services.json").read_text())["services"]
    stock = json.loads((scene / "rolling_stock.json").read_text())
    scenarios = json.loads((scene / "scenarios.json").read_text())
    scenario = next(item for item in scenarios["scenarios"]
                    if item["id"] == scenarios["default_scenario_id"])
    duration = manifest["simulation_settings"]["duration_seconds"]
    entries = []
    warnings = 0
    for service in services:
        repeat = service.get("repeat", {})
        # The creator fixture uses explicit repeat counts and entry times.
        count = repeat["count"] if repeat else 1
        entries.extend(service["entry_time_seconds"] + index * repeat.get("headway_seconds", 0)
                       for index in range(count))
        for stop in service["stops"]:
            if "planned_arrival_seconds" in stop and "planned_departure_seconds" in stop:
                warnings += int(stop["dwell_seconds"] > stop["planned_departure_seconds"] - stop["planned_arrival_seconds"])
    signalling = json.loads((scene / "signalling.json").read_text())
    unsignalled = 0
    if not signalling.get("signalling_areas"):
        # Without any area, every distinct route section is reported once.
        unsignalled = len({block for route in signalling["routes"] for block in route["blocks"]})
        warnings += 1
    in_period = sum(0 <= entry < duration for entry in entries)
    counts = {"Service definitions": len(services), "Configured total": len(entries),
              "Number of services in sim.": in_period, "Selected": len(entries),
              "Selected in period": in_period, "Compositions": len(stock["compositions"]),
              "Incidents": len(scenario["incidents"])}
    start = datetime.strptime(manifest["base_time"], "%H:%M:%S")
    summary = {"Selected in period": str(in_period), "Start clock": start.strftime("%H:%M:%S"),
               "Duration": f"{duration:g} s elapsed (ends near {(start + timedelta(seconds=duration)):%H:%M:%S})",
               "Active incidents": str(counts["Incidents"])}
    details = {"Case study": manifest["name"], "Scenario": scenario["id"],
               **{label: str(value) for label, value in counts.items()},
               "Counting rule": f"scheduled entry ≥ 0 and < {duration:g} s; these are configured identities, not observed trains."}
    return summary, details, warnings, unsignalled


def contains(outer: list, inner: list) -> bool:
    x, y, width, height = outer
    ix, iy, iw, ih = inner
    return width > 0 and height > 0 and iw > 0 and ih > 0 and (x <= ix and y <= iy
            and ix + iw <= x + width and iy + ih <= y + height)


def check_review(review: dict, scene: Path, advanced: bool, cancellation: str,
                 enlarged: bool = False) -> None:
    rendered = json.loads(review["E2E_RUN_REVIEW_RENDERED"])
    summary, details, warnings, unsignalled = expected_review(scene)
    assert rendered["summary"] == summary, rendered
    actual = dict(line.split(": ", 1) for line in rendered["details"].splitlines())
    for label, value in details.items():
        assert actual.get(label) == value, (label, value, actual)
    if advanced:
        assert actual["Validation"] == f"0 error(s), {warnings} warning(s)", actual
    else:
        assert "Validation" not in actual, actual
        assert "Incident configuration" not in actual, actual
        assert "Entrance delay configuration" not in actual, actual
    scenarios = json.loads((scene / "scenarios.json").read_text())
    scenario = next(item for item in scenarios["scenarios"] if item["id"] == details["Scenario"])
    if advanced and int(details["Incidents"]):
        for incident in scenario["incidents"]:
            expected = (f"id={incident['id']} type={incident['type']} target={incident['target']} "
                        f"start={incident['start_seconds']:g} window={incident['end_seconds']:g} "
                        f"occurrence={incident.get('occurrence', 'all')}")
            assert expected in actual["Incident configuration"], actual
    assert rendered["context"] == f"{details['Case study']} / {details['Scenario']}", rendered
    assert rendered["plainContext"] and rendered["plainDetails"], rendered
    # Inspect default semantics without pressing Enter and starting a simulation.
    assert rendered["initialFocus"] == "run" and rendered["runDefault"], rendered
    assert not rendered["cancelDefault"], rendered
    for state in ("collapsedGeometry", "expandedGeometry", "scrolledGeometry", "recollapsedGeometry"):
        geometry = rendered[state]
        dialog, footer, scroll = geometry["dialog"], geometry["footer"], geometry["scroll"]
        assert dialog[2] <= geometry["maximum"][0] and dialog[3] <= geometry["maximum"][1], geometry
        screen_width, screen_height = rendered["availableScreen"]
        assert dialog[2] <= screen_width * 9 // 10 and dialog[3] <= screen_height * 4 // 5, geometry
        assert geometry["footerVisible"] and contains(dialog, footer), geometry
        assert contains(footer, geometry["run"]) and contains(footer, geometry["cancel"]), geometry
        assert contains(dialog, scroll) and scroll[1] + scroll[3] <= footer[1], geometry
        if enlarged:
            assert geometry["fontPoints"] == 18 and dialog[2] <= 560 and dialog[3] <= 420, geometry
            assert geometry["controlFontPoints"] == [18, 18, 18, 18], geometry
    assert rendered["expandedGeometry"]["footer"] == rendered["scrolledGeometry"]["footer"], rendered
    keyboard = rendered["keyboard"]
    assert keyboard["bodyFocus"] == (["Loaded Data", "Validation"] if advanced else []), keyboard
    assert keyboard["tabReachedFooter"] and keyboard["backtabReturned"], keyboard
    assert contains(keyboard["viewport"], keyboard["focusedBodyRect"]), keyboard
    if enlarged:
        assert rendered["expandedGeometry"]["scrollMaximum"] > 0, rendered
        assert keyboard["scrollValue"] > 0, keyboard
    for state in ("collapsed", "recollapsed"):
        assert rendered[state] == {"checked": False, "visible": False, "arrow": "right"}, rendered
    assert rendered["expanded"] == {"checked": True, "visible": True, "arrow": "down"}, rendered
    zero = summary["Selected in period"] == "0"
    status = ("Ready to run, but no selected services enter during this period." if zero else
              f"Ready to run, but {unsignalled} route {'section has' if unsignalled == 1 else 'sections have'} "
              "no signalling level. Trains there run without signalling." if unsignalled else
              f"Ready to run. Review {warnings} validation {'warning' if warnings == 1 else 'warnings'} if needed."
              if advanced and warnings else "Ready to run.")
    assert rendered["status"] == status, rendered
    assert rendered["warning"] == (zero or unsignalled > 0 or (advanced and warnings > 0)), rendered
    assert rendered["runEnabled"] and rendered["rejected"] and not rendered["workerStarted"], rendered
    assert rendered["cancellation"] == cancellation, rendered
    assert review["E2E_RUN_REVIEW_ADVANCED"] == str(int(advanced)), review
    assert review["E2E_RUN_REVIEW_VALIDATION_VISIBLE"] == str(int(advanced)), review
    assert review["E2E_RUN_REVIEW_CANCELLED"] == "1", review
    assert review["E2E_RUN_REVIEW_UNCHANGED"] == "1", review


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
        for variant in ("baseline", "boundaries", "zero-in-period", "warning", "incidents", "long-context",
                        "no-signalling-area"):
            fixture = settings / variant
            shutil.copytree(scene, fixture)
            services_path = fixture / "services.json"
            services = json.loads(services_path.read_text())
            service = services["services"][0]
            duration = json.loads((fixture / "scene.json").read_text())["simulation_settings"]["duration_seconds"]
            if variant == "boundaries":
                service["entry_time_seconds"] = 0
                service["repeat"]["headway_seconds"] = duration / 2
            elif variant == "zero-in-period":
                shift = duration - service["entry_time_seconds"]
                service["entry_time_seconds"] = duration
                for stop in service["stops"]:
                    for field in ("planned_arrival_seconds", "planned_departure_seconds"):
                        if field in stop:
                            stop[field] += shift
            elif variant == "no-signalling-area":
                signalling_path = fixture / "signalling.json"
                signalling = json.loads(signalling_path.read_text())
                del signalling["signalling_areas"]
                signalling_path.write_text(json.dumps(signalling))
            elif variant == "warning":
                stop = service["stops"][0]
                stop["dwell_seconds"] = stop["planned_departure_seconds"] - stop["planned_arrival_seconds"] + 1
            elif variant in ("incidents", "long-context"):
                scenarios_path = fixture / "scenarios.json"
                scenarios = json.loads(scenarios_path.read_text())
                selected = next(item for item in scenarios["scenarios"] if item["incidents"])
                if variant == "long-context":
                    manifest_path = fixture / "scene.json"
                    manifest = json.loads(manifest_path.read_text())
                    manifest["name"] = ("Case authored & " + "long railway context " * 9).rstrip()
                    manifest_path.write_text(json.dumps(manifest))
                    selected["id"] = "scenario-<authored>-&-" + "long-context-" * 14
                    for incident in selected["incidents"]:
                        incident["id"] += "-<authored>-&-" + "long-incident-context-" * 40
                scenarios["default_scenario_id"] = selected["id"]
                scenarios_path.write_text(json.dumps(scenarios))
            services_path.write_text(json.dumps(services))
            for advanced in (False, True):
                for cancellation in ("button", "escape"):
                    run_settings = settings / f"settings-{variant}-{advanced}-{cancellation}"
                    ini = run_settings / "EGTRAIN/EGTRAIN.ini"
                    ini.parent.mkdir(parents=True, exist_ok=True)
                    ini.write_text(f"[General]\nadvancedDetails={'true' if advanced else 'false'}\n")
                    enlarged = variant == "long-context"
                    check_review(launch(app, run_settings, fixture, cancellation, enlarged),
                                 fixture, advanced, cancellation, enlarged)
            print(f"{variant}: normal/advanced details, geometry, keyboard, readiness, Cancel and Escape passed")
    print("chooser and run-review presentation contract passed")


if __name__ == "__main__":
    main()
