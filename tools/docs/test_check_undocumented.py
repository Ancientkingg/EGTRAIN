#!/usr/bin/env python3
import unittest

from check_undocumented import new_warnings, parse_warnings

BASE_LOG = """\
/base/EGTRAIN/QEGTRAIN/scene/A.h:10: warning: Member size() (function) of class A is not documented.
/base/EGTRAIN/QEGTRAIN/scene/Old.h:4: warning: Compound Old is not documented.
/base/EGTRAIN/QEGTRAIN/scene/C.h:2: warning: Compound C is not documented.
"""

HEAD_LOG = """\
/head/EGTRAIN/QEGTRAIN/scene/A.h:12: warning: Member size() (function) of class A is not documented.
/head/EGTRAIN/QEGTRAIN/scene/A.h:14: warning: Member clear() (function) of class A is not documented.
/head/EGTRAIN/QEGTRAIN/scene/A.h:20: warning: argument 'x' of command @param is not found
/head/EGTRAIN/QEGTRAIN/scene/New.h:4: warning: Compound Old is not documented.
/head/EGTRAIN/QEGTRAIN/scene/B.h:3: warning: Compound B is not documented.
/head/EGTRAIN/QEGTRAIN/scene/C.h:2: warning: Compound C is not documented.
"""


class NewWarningsTest(unittest.TestCase):
    def test_reports_only_new_warnings_in_changed_files(self):
        base = parse_warnings(BASE_LOG, "/base")
        head = parse_warnings(HEAD_LOG, "/head")
        # A.h gains clear() and moves size(), and Old.h is renamed to New.h. B.h and C.h are not changed.
        changed = {
            "EGTRAIN/QEGTRAIN/scene/A.h": "EGTRAIN/QEGTRAIN/scene/A.h",
            "EGTRAIN/QEGTRAIN/scene/New.h": "EGTRAIN/QEGTRAIN/scene/Old.h",
        }
        self.assertEqual(
            new_warnings(base, head, changed),
            ["EGTRAIN/QEGTRAIN/scene/A.h: Member clear() (function) of class A is not documented."],
        )


if __name__ == "__main__":
    unittest.main()
