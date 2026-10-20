#!/usr/bin/env python3
"""Isolated offscreen application boundary checks with a scripted reply, not TLS."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

from jsonschema import Draft202012Validator, FormatChecker
from test_telemetry_contract import validate_wire

ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Copenhagen"
MODES = ("mixed", "usage", "diagnostics", "late", "revoke", "offline", "retired",
         "stop_before", "stop_postprocessing", "teardown", "replacement", "repeat", "zero", "short_horizon", "completion_stop", "run_revoke", "discard", "bundle", "table_failure", "unsupported", "import_late", "import_revoke", "malformed_bundle", "install_failure")


def run(executable, root, mode):
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM="offscreen", QEGTRAIN_AUTOSTART="1",
               QEGTRAIN_E2E_TELEMETRY_SMOKE=str(root), QEGTRAIN_E2E_TELEMETRY_MODE=mode,
               QEGTRAIN_E2E_SETTINGS_DIR=str(root / "other-settings"),
               QEGTRAIN_OUTPUT_DIR=str(root / "output"))
    # Native file choosers are not used on the offscreen Qt platform.
    args = [str(executable), "--scene", str(root / "case"), "-h", "1" if mode == "short_horizon" else "200", "-g", "1",
            "-pax", "0", "-TSM", "0", "-RC", "0"]
    with (root / f"{mode}.log").open("wb") as log:
        result = subprocess.run(args, cwd=ROOT / "EGTRAIN/QEGTRAIN", env=env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=150)
    text = (root / f"{mode}.log").read_text(errors="replace")
    assert result.returncode == 0, text[-4000:]
    assert "E2E_TELEMETRY_MOCK_REFUSED" not in text, text[-4000:]
    assert f"E2E_TELEMETRY_ACTIONS_DONE mode={mode}" in text, text[-4000:]
    assert "E2E_TELEMETRY_SHUTDOWN" in text, text[-4000:]
    if mode in ("import_late", "import_revoke"):
        assert "E2E_TELEMETRY_IMPORTED" in text, text[-4000:]
        assert json.loads((root / "imported/scene.json").read_text())["name"] == "private-legacy"
    if mode == "install_failure":
        assert "E2E_TELEMETRY_INSTALL_PROBES runs=5 failures=4" in text, text[-4000:]
        for ordinal in range(5):
            assert (root / f"install_run_{ordinal}.csv").read_bytes() == (root / "summary.csv").read_bytes()
            assert (root / f"install_run_{ordinal}.energy").read_bytes() == (root / "install_run_4.energy").read_bytes()
    return text


def inspect(root, validator):
    events = {}
    envelopes = []
    for file in sorted((root / "payloads").glob("*.json")):
        body = file.read_bytes()
        data = validate_wire(body, validator)
        envelopes.append(data)
        # Fixture names, paths and CSV/provider contents must never enter a payload.
        for forbidden in (str(root).encode(), b"private", b"Copenhagen", b"chart.csv", b"sidecar"):
            assert forbidden not in body, f"private fixture data in {file}"
        for event in data["events"]:
            key = event["event_id"]
            value = (data["category"], event)
            assert key not in events or events[key] == value, "retry changed event content"
            events[key] = value
    # Offline events may still be queued after the last attempted batch. Validate
    # each durable event by wrapping it in the same closed wire contract.
    for category in ("usage", "diagnostics"):
        file = root / "queue" / f"{category}.json"
        if not file.exists():
            continue
        for record in json.loads(file.read_text())["events"]:
            event = record["event"]
            application = envelopes[0]["application"] if envelopes else {
                "name": "EGTRAIN", "version": "1.0.0", "platform": "macos", "architecture": "arm64"}
            data = dict(schema_version=1, category=category, application=application,
                        sent_at=event["occurred_at"], events=[event])
            if category == "usage":
                data["installation_id"] = record["stamp"]
            validate_wire(json.dumps(data).encode(), validator)
            value = (category, event)
            assert event["event_id"] not in events or events[event["event_id"]] == value
            events[event["event_id"]] = value
    counts = Counter((category, event["name"], tuple(sorted(event["properties"].items())))
                     for category, event in events.values())
    return counts, envelopes


def assert_counts(mode, counts):
    if mode.startswith("retired"):
        if mode == "retired_restart":
            assert not counts, counts
        else:
            assert sum(counts.values()) == 1, counts
            assert counts[("usage", "session.started", ())] == 1, counts
        return
    simple = Counter()
    for (category, name, props), count in counts.items():
        # Duration is deliberately bucketed; it is not a stable timing assertion.
        if name.startswith("simulation."):
            props = ()
        simple[(category, name, props)] += count
    expected = Counter()
    if mode != "diagnostics":
        expected.update({("usage", "session.started", ()): 1,
                         ("usage", "scene.opened", (("scene_kind", "local"),)): 3,
                         ("usage", "editor.opened", ()): 7,
                         ("usage", "simulation.started", ()): 1,
                         ("usage", "export.completed", (("export_kind", "csv"),)): 4,
                         ("usage", "export.completed", (("export_kind", "png"),)): 3,
                         ("usage", "export.completed", (("export_kind", "scene_bundle"),)): 1,
                         ("usage", "export.failed", (("export_kind", "csv"),)): 1,
                         ("usage", "export.failed", (("export_kind", "scene_bundle"),)): 1})
        if mode not in ("stop_before", "stop_postprocessing", "teardown", "run_revoke"):
            expected[("usage", "simulation.completed", ())] = 1
        if mode == "bundle":
            expected[("usage", "scene.opened", (("scene_kind", "bundled"),))] = 1
        if mode == "table_failure":
            expected[("usage", "export.failed", (("export_kind", "png"),))] = 1
        if mode == "repeat":
            expected[("usage", "simulation.started", ())] += 1
            expected[("usage", "simulation.completed", ())] += 1
        if mode == "replacement":
            expected[("usage", "scene.opened", (("scene_kind", "local"),))] += 1
            expected[("usage", "simulation.started", ())] += 1
        if mode == "zero":
            del expected[("usage", "simulation.started", ())]
            del expected[("usage", "simulation.completed", ())]
        if mode in ("stop_before", "stop_postprocessing", "teardown", "zero", "discard"):
            expected[("usage", "export.completed", (("export_kind", "csv"),))] -= 1
            expected[("usage", "export.completed", (("export_kind", "png"),))] -= 1
    if mode != "usage":
        expected.update({("diagnostics", "operation.failed", (("error_code", "invalid_input"), ("operation_code", "scene_open"))): 1,
                         ("diagnostics", "operation.failed", (("error_code", "invalid_input"), ("operation_code", "simulation"))): 1,
                         ("diagnostics", "operation.failed", (("error_code", "io_failure"), ("operation_code", "export"))): 1,
                         ("diagnostics", "operation.failed", (("error_code", "unsupported_format"), ("operation_code", "export"))): 1})
        if mode == "table_failure":
            expected[("diagnostics", "operation.failed", (("error_code", "io_failure"), ("operation_code", "export")))] += 1
        if mode == "unsupported":
            expected[("diagnostics", "operation.failed", (("error_code", "unsupported_format"), ("operation_code", "scene_open")))] = 1
        if mode == "zero":
            expected[("diagnostics", "operation.failed", (("error_code", "invalid_input"), ("operation_code", "simulation")))] += 1
    assert simple == expected, f"{mode}: actual={simple}\nexpected={expected}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--mode", choices=MODES)
    parser.add_argument("--keep", action="store_true", help="retain private payloads for inspection")
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    schema = json.loads((ROOT / "docs/telemetry/batch-v1.schema.json").read_text())
    validator = Draft202012Validator(schema, format_checker=FormatChecker(formats=["date-time", "uuid"]))
    parent = Path(tempfile.mkdtemp(prefix="egtrain-telemetry-smoke-"))
    passed = False
    outputs = []
    try:
        for mode in ((args.mode,) if args.mode else MODES):
            root = parent / mode
            root.mkdir()
            shutil.copytree(SCENE, root / "case")
            manifest = root / "case/scene.json"
            data = json.loads(manifest.read_text())
            data["simulation_settings"]["duration_seconds"] = 40000.0
            manifest.write_text(json.dumps(data, indent=2) + "\n")
            if mode == "short_horizon":
                services_path = root / "case/services.json"
                services = json.loads(services_path.read_text())
                for service in services["services"]:
                    if "entry_time_seconds" in service:
                        service["entry_time_seconds"] += 5003
                    for stop in service.get("stops", []):
                        for field in ("planned_arrival_seconds", "planned_departure_seconds"):
                            if field in stop:
                                stop[field] += 5003
                services_path.write_text(json.dumps(services, indent=2) + "\n")
            shutil.copytree(root / "case", root / "invalid")
            (root / "invalid/infrastructure.json").write_text("{not json")
            (root / "ambiguous").mkdir()
            (root / "ambiguous/scene.json").write_text("{not json")
            (root / "newer").mkdir()
            newer = dict(data, schema_version=data["schema_version"] + 1)
            (root / "newer/scene.json").write_text(json.dumps(newer))
            for kind in ("missing", "noninteger"):
                bundle_manifest = dict(data, format="egscene", bundle_version=2)
                if kind == "missing": del bundle_manifest["schema_version"]
                else: bundle_manifest["schema_version"] = "private-malformed-version"
                with zipfile.ZipFile(root / f"{kind}-schema.egscene", "w", compression=zipfile.ZIP_DEFLATED) as archive:
                    for file in (root / "case").glob("*.json"):
                        archive.writestr(file.name, json.dumps(bundle_manifest) if file.name == "scene.json" else file.read_bytes())
            legacy = root / "private-legacy"
            (legacy / "B0").mkdir(parents=True)
            (legacy / "Stations.txt").write_text("0\tFlatStart\n1.5\tFlatEnd\n")
            (legacy / "Connections.txt").write_text("0\t0.5\t1\t0.5\n")
            (legacy / "TrackandStations.txt").write_text("0\nFlatStart FlatEnd\n")
            (legacy / "B0/NodiCumPari.txt").write_text("1\t0\t0\n2\t1\t0\n")
            (root / "imported").mkdir()
            run(executable, root, mode)
            counts, envelopes = inspect(root, validator)
            assert_counts(mode, counts)
            assert (root / "chart.csv").read_bytes() == b"private,content\n1,2\n"
            assert (root / "table.csv").read_bytes() == b"private,table\n"
            assert not (root / "blocked.csv").exists(), "sidecar failure published the artifact"
            assert (root / "chart.csv.provenance.json").is_file()
            if mode not in ("stop_before", "stop_postprocessing", "teardown", "retired", "zero", "short_horizon", "discard"):
                energies = sorted((root / "output").rglob("EnergyConsumptionPerTrain.txt"))
                assert len(energies) == 1, energies
                outputs.append((mode, (root / "summary.csv").read_bytes(), energies[0].read_bytes()))
            print(f"{mode}: {sum(counts.values())} distinct validated events in {len(envelopes)} scripted payloads")
            if mode == "retired":
                assert json.loads((root / "queue/control.json").read_text())["retired"]
                (root / "payloads").rename(root / "retirement-payloads")
                run(executable, root, "retired_restart")
                restart, _ = inspect(root, validator)
                assert_counts("retired_restart", restart)
                print("retired restart: no payload or queued event")
        if len(outputs) > 1:
            reference = outputs[0]
            for mode, csv, energy in outputs[1:]:
                assert csv == reference[1] and energy == reference[2], f"deterministic output differs in {mode}"
            print("deterministic summary CSV and energy bytes unchanged across consent/transport modes")
        passed = True
    finally:
        if passed and not args.keep:
            shutil.rmtree(parent)
        else:
            print(f"private telemetry smoke evidence retained: {parent}")


if __name__ == "__main__":
    main()
