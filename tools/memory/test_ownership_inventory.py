#!/usr/bin/env python3
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ownership_inventory


class TestOwnershipInventory(unittest.TestCase):
    def test_real_inventory_has_all_categories(self):
        root = Path(__file__).resolve().parents[2]
        results = ownership_inventory.scan_codebase(root)
        self.assertTrue(results["Owning allocations"])
        self.assertTrue(results["Qt-managed allocations"])
        self.assertTrue(results["Unclassified allocations"])
        self.assertTrue(results["Oversized arrays"])
        observer_lines = "\n".join(item[2] for item in results["Non-owning pointers"])
        self.assertIn("trainPaxItem", observer_lines)
        self.assertIn("paxIconItem", observer_lines)
        self.assertIn("TDS_arc", observer_lines)
        self.assertTrue(results["Unclassified raw pointers"])
        for entries in results.values():
            self.assertEqual(entries, sorted(entries, key=lambda item: (item[0], item[1])))
            self.assertFalse(any("third_party" in item[0] for item in entries))

    def test_fixture_classification_and_exclusions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "EGTRAIN/QEGTRAIN"
            (source / "app").mkdir(parents=True)
            (source / "simulation").mkdir()
            fixture = source / "simulation/Fixture.cpp"
            fixture.write_text(
                '#define LIMIT 300\n'
                'auto* buffer = new double[20];\n'
                'auto* unknown_owner = new Widget;\n'
                'auto* child = new QWidget(parent);\n'
                'auto* text = new QString(parent);\n'
                'auto* parentless = new QWidget();\n'
                'Widget* unknown;\n'
                'int newId = 1;\n'
                'double a[200], b[LIMIT];\n'
                'char storage[500];\n'
                'signalling_block_sections[562].total_nodes = 0;\n'
                'new (storage) Widget;\n'
                'auto text = R"tag(new HiddenWidget; int hidden[900];)tag";\n'
            )
            (source / "app/MainWindow.h").write_text(
                "class QGraphicsItemGroup;\n"
                "class TrainItemGroup;\n"
                "class PassengerItem;\n"
                "QGraphicsItemGroup* trainPaxInfoItem;\n"
                "TrainItemGroup* trainPaxItem;\n"
                "QGraphicsItemGroup* paxIconInfoItem;\n"
                "PassengerItem* paxIconItem;\n"
            )
            for excluded in ("io/third_party", "generated", "vendor", "build-debug", "cmake-build-debug"):
                path = source / excluded
                path.mkdir(parents=True)
                (path / "Noise.cpp").write_text("auto* noise = new Noise; int huge[999];\n")

            results = ownership_inventory.scan_codebase(root)
            owning = "\n".join(item[2] for item in results["Owning allocations"])
            qt_managed = "\n".join(item[2] for item in results["Qt-managed allocations"])
            unclassified_allocations = "\n".join(
                item[2] for item in results["Unclassified allocations"]
            )
            arrays = "\n".join(item[2] for item in results["Oversized arrays"])
            paths = "\n".join(item[0] for entries in results.values() for item in entries)

            self.assertEqual(owning, "auto* buffer = new double[20];")
            self.assertEqual(qt_managed, "auto* child = new QWidget(parent);")
            self.assertIn("unknown_owner = new Widget", unclassified_allocations)
            self.assertIn("text = new QString(parent)", unclassified_allocations)
            self.assertIn("parentless = new QWidget()", unclassified_allocations)
            self.assertNotIn("newId", "\n".join(
                item[2] for entries in results.values() for item in entries
            ))
            self.assertIn("double a[200], b[LIMIT];", arrays)
            self.assertIn("char storage[500];", arrays)
            self.assertNotIn("HiddenWidget", owning)
            self.assertNotIn("hidden[900]", arrays)
            self.assertNotIn("signalling_block_sections[562]", arrays)
            self.assertNotIn("Noise.cpp", paths)
            self.assertEqual(len(results["Non-owning pointers"]), 4)
            self.assertEqual(
                [item[2] for item in results["Unclassified raw pointers"]],
                ["Widget* unknown;"],
            )


if __name__ == "__main__":
    unittest.main()
