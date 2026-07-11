import os
import sys
import unittest

# Add root folder to sys.path so we can import tools/memory/ownership_inventory.py
current_dir = os.path.dirname(os.path.abspath(__file__))
project_root = os.path.abspath(os.path.join(current_dir, "..", ".."))
sys.path.insert(0, os.path.join(project_root, "tools", "memory"))

class TestOwnershipInventory(unittest.TestCase):
    def test_headings_and_known_hits(self):
        # We try to import ownership_inventory
        import ownership_inventory

        # Scan the codebase using project_root
        results = ownership_inventory.scan_codebase(project_root)

        # Verify all headings exist in the results dictionary
        self.assertIn("Owning allocations", results)
        self.assertIn("Oversized arrays", results)
        self.assertIn("Non-owning pointers", results)

        # Verify third-party files are excluded
        for category, items in results.items():
            for path, line_no, content in items:
                self.assertNotIn("third_party", path, f"Found third-party file in {category}: {path}")
                self.assertNotIn("pugixml", path, f"Found pugixml in {category}: {path}")

        # Check at least one known source hit per category
        # 1. Owning allocations:
        # EGTRAIN/QEGTRAIN/simulation/Infrastructure.cpp contains "Matr = new double*[NODS];" or similar
        owning_hits = [
            (path, line_no, content)
            for path, line_no, content in results["Owning allocations"]
            if "Infrastructure.cpp" in path and "new double" in content
        ]
        self.assertTrue(len(owning_hits) >= 1, "Expected at least one 'new double' allocation in Infrastructure.cpp")

        # 2. Oversized arrays:
        # EGTRAIN/QEGTRAIN/simulation/Signalling.cpp contains "Section signalling_block_sections[6000];"
        array_hits = [
            (path, line_no, content)
            for path, line_no, content in results["Oversized arrays"]
            if ("Signalling.cpp" in path and "signalling_block_sections" in content) or \
               ("Infrastructure.h" in path and "blockSets" in content) or \
               ("Infrastructure.h" in path and "A[1500]" in content)
        ]
        self.assertTrue(len(array_hits) >= 1, "Expected at least one oversized array hit in Signalling.cpp or Infrastructure.h")

        # 3. Non-owning pointers:
        # EGTRAIN/QEGTRAIN/simulation/Signalling.h contains "Node* nodelist_of_nodes_in_signalling_section;"
        pointer_hits = [
            (path, line_no, content)
            for path, line_no, content in results["Non-owning pointers"]
            if "Signalling.h" in path and "nodelist_of_nodes_in_signalling_section" in content
        ]
        self.assertTrue(len(pointer_hits) >= 1, "Expected at least one non-owning pointer hit in Signalling.h")

        # Check stable sorted output
        for category in ["Owning allocations", "Oversized arrays", "Non-owning pointers"]:
            items = results[category]
            sorted_items = sorted(items, key=lambda x: (x[0], x[1]))
            self.assertEqual(items, sorted_items, f"Category {category} is not sorted stably")

if __name__ == "__main__":
    unittest.main()
