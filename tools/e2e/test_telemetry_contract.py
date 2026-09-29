#!/usr/bin/env python3
"""Offline contract and strict wire-boundary checks; no network or app dependency."""

import copy
import json
import unittest
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker, ValidationError


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "docs/telemetry"
MAX_BYTES = 65536


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def nonstandard_number(value):
    raise ValueError("nonstandard number: " + value)


def validate_wire(body, validator):
    if len(body) > MAX_BYTES:
        raise ValueError("wire body exceeds 65536 bytes")
    data = json.loads(body.decode("utf-8"), object_pairs_hook=unique_pairs,
                      parse_constant=nonstandard_number)
    validator.validate(data)
    ids = [event["event_id"] for event in data["events"]]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate event_id in batch")
    return data


class TelemetryContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema = json.loads((CONTRACT / "batch-v1.schema.json").read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(cls.schema)
        cls.validator = Draft202012Validator(cls.schema, format_checker=FormatChecker(formats=["date-time", "uuid"]))
        cls.usage = validate_wire((CONTRACT / "examples/valid-usage.json").read_bytes(), cls.validator)
        cls.diagnostics = validate_wire((CONTRACT / "examples/valid-diagnostics.json").read_bytes(), cls.validator)

    def assert_invalid(self, data):
        with self.assertRaises(ValidationError):
            self.validator.validate(data)

    def test_examples(self):
        for path in (CONTRACT / "examples").glob("*.json"):
            with self.subTest(path=path.name):
                if path.name.startswith("valid-"):
                    validate_wire(path.read_bytes(), self.validator)
                else:
                    with self.assertRaises(ValidationError):
                        validate_wire(path.read_bytes(), self.validator)

    def test_each_vocabulary_branch(self):
        kinds = {
            "session.started": {}, "scene.opened": {"scene_kind": "local"},
            "editor.opened": {}, "simulation.started": {},
            "simulation.completed": {"duration_bucket": "under_1s"},
            "simulation.failed": {"duration_bucket": "10m_or_more"},
            "export.completed": {"export_kind": "csv"},
            "export.failed": {"export_kind": "png"},
        }
        schema_names = self.schema["$defs"]["usageEvent"]["properties"]["name"]["enum"]
        self.assertEqual(set(schema_names), set(kinds))
        self.assertEqual(self.schema["$defs"]["diagnosticEvent"]["properties"]["name"]["const"], "operation.failed")
        for name, properties in kinds.items():
            with self.subTest(name=name):
                data = copy.deepcopy(self.usage)
                data["events"] = [dict(data["events"][0], name=name, properties=properties)]
                self.validator.validate(data)
                data["events"][0]["properties"]["private_text"] = "/home/user/file"
                self.assert_invalid(data)

    def test_closed_objects_and_missing_ids(self):
        for category, original in (("usage", self.usage), ("diagnostics", self.diagnostics)):
            for location in ((), ("application",), ("events", 0), ("events", 0, "properties")):
                with self.subTest(category=category, location=location):
                    data = copy.deepcopy(original)
                    target = data
                    for part in location:
                        target = target[part]
                    target["unknown"] = "private/path"
                    self.assert_invalid(data)
            for field in ("event_id", "occurred_at"):
                data = copy.deepcopy(original)
                del data["events"][0][field]
                self.assert_invalid(data)
        data = copy.deepcopy(self.usage)
        del data["installation_id"]
        self.assert_invalid(data)
        data = copy.deepcopy(self.diagnostics)
        data["installation_id"] = self.usage["installation_id"]
        self.assert_invalid(data)
        data = copy.deepcopy(self.usage)
        data["events"] = self.diagnostics["events"]
        self.assert_invalid(data)

    def test_types_formats_and_private_text(self):
        for field, invalid_values in {
            "sent_at": ["2026-02-30T10:00:00Z", "2026-01-20T10:00:00+00:00", "2026-01-20T25:00:00Z", "2026-01-20T10:00:00Z\n"],
            "installation_id": ["5b54c480-518e-1d47-93f9-b501c2e8d58d", "5b54c480-518e-4d47-73f9-b501c2e8d58d", "NOT-A-UUID"],
        }.items():
            for value in invalid_values:
                data = copy.deepcopy(self.usage)
                data[field] = value
                self.assert_invalid(data)
        for field in ("event_id", "occurred_at"):
            data = copy.deepcopy(self.usage)
            data["events"][0][field] = "/private/scene.egscene"
            self.assert_invalid(data)
        for version in ("1.2.3+private-path", "01.2.3", "1.2.3-beta", "1234567890.2.3", "1.2.3\n"):
            data = copy.deepcopy(self.usage)
            data["application"]["version"] = version
            self.assert_invalid(data)
        for key, value in (("operation_code", "read /home/user/data"), ("error_code", "exception details")):
            data = copy.deepcopy(self.diagnostics)
            data["events"][0]["properties"][key] = value
            self.assert_invalid(data)
        for field, value in (("schema_version", 2), ("category", "diagnostics")):
            data = copy.deepcopy(self.usage)
            data[field] = value
            self.assert_invalid(data)

    def test_event_and_wire_limits(self):
        data = copy.deepcopy(self.usage)
        data["events"] = []
        self.assert_invalid(data)
        data["events"] = [dict(self.usage["events"][0], event_id=f"00000000-0000-4000-8000-{i:012x}") for i in range(50)]
        self.validator.validate(data)
        data["events"].append(dict(data["events"][0], event_id="00000000-0000-4000-8000-000000000050"))
        self.assert_invalid(data)
        data = self.usage
        body = json.dumps(data, separators=(",", ":")).encode("utf-8")
        validate_wire(body + b" " * (MAX_BYTES - len(body)), self.validator)
        with self.assertRaises(ValueError):
            validate_wire(body + b" " * (MAX_BYTES + 1 - len(body)), self.validator)
        repeated = copy.deepcopy(data)
        repeated["events"].append(copy.deepcopy(repeated["events"][0]))
        with self.assertRaises(ValueError):
            validate_wire(json.dumps(repeated).encode(), self.validator)

    def test_strict_wire(self):
        body = json.dumps(self.usage).encode("utf-8")
        for invalid in (body + b"\xff", b'{"schema_version":1,"schema_version":1}',
                        body.replace(b'"schema_version": 1', b'"schema_version": NaN'),
                        b'{"schema_version": Infinity}', b'{'):
            with self.subTest(invalid=invalid[:40]), self.assertRaises((ValueError, UnicodeError)):
                validate_wire(invalid, self.validator)


if __name__ == "__main__":
    unittest.main()
