#!/usr/bin/env python3
"""Tests of tools/docs/home_data.py: the values of docs/site/home-data.json that come from the bundled case, and the
reading and thinning of a run."""
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import home_data  # noqa: E402

HEADER = "Time[s]\tSpeed[m/s]\tPosition[m]\tTail_Position[m]\tPower_Cons[kW]\tBX[m]\tinstant_train_energy_consumption[KWh]\tBlock\n"
SWITCH = "@12-B2@-25.240000/@0-B11@-25.260000"
# The values that come from the case alone, besides those of the train graph.
CASE_VALUES = ("km_from", "km_to", "stations", "drawing_width", "drawing_height", "drawing_min_width", "drawing_font",
               "drawing_centre", "drawing_tracks", "drawing_stations", "drawing_scale", "station_rows", "capture_seconds")


def write_run(folder: Path) -> None:
    """A run of one train at 20 m/s from 1001 s to 1025 s, on track B2 and then, after a switch, on B11, and the two
    sections of track that it reserves."""
    (folder / "TEMP").mkdir(parents=True)
    (folder / "TrainTrajectories").mkdir()
    rows = []
    for t in range(1001, 1026):
        block = "@1-B2@" if t < 1013 else SWITCH if t < 1016 else "@0-B11@"
        rows.append(f"{t}\t20\t{25000 + 20 * (t - 1001)}\t{24825 + 20 * (t - 1001)}\t0\t0\t0\t{block}\n")
    (folder / "TEMP/Traj_Train_10450-1.txt").write_text(HEADER + "".join(rows), encoding="utf-8")
    (folder / "TrainTrajectories/BlockingTimes.txt").write_text(
        f"10450-1\n@1-B2@ {SWITCH}\n25000 25240\n25240 26000\n990 1005\n1012 1040.5\n", encoding="utf-8")


class HomeData(unittest.TestCase):
    def setUp(self):
        self.case = home_data.read_case(home_data.CASE)
        self.committed = json.loads(home_data.OUT.read_text(encoding="utf-8"))
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)

    def test_the_train_graph_is_that_of_the_committed_case(self):
        graph = home_data.graph(self.case)
        self.assertEqual({key: self.committed[key] for key in graph}, graph)

    def test_a_run_is_read_every_10_s_with_its_first_and_last_sample(self):
        write_run(self.folder)
        trains, blocking = home_data.read_run(self.folder, self.case["level"])
        b2, b11 = self.case["level"]["B2"], self.case["level"]["B11"]
        # The sample after the switch takes the level of B11, the track that the train runs onto.
        self.assertEqual(trains, [("10450-1", [[1001, 25.0, b2, 72], [1010, 25.18, b2, 72], [1020, 25.38, b11, 72], [1025, 25.48, b11, 72]])])
        self.assertEqual(blocking, [[[25.0, 25.24, 990.0, 1012.0, b2], [25.24, 26.0, 1005.0, 1040.5, b2]]])

    def test_thinning_keeps_the_ends_and_each_sample_that_a_straight_line_misses(self):
        # A train that runs at 72 km/h, stops for 40 s and runs on: time, km and speed.
        samples = [(0, 0.0, 72), (10, 0.2, 72), (20, 0.4, 72), (30, 0.55, 36), (40, 0.6, 0), (50, 0.6, 0), (60, 0.6, 0),
                   (70, 0.6, 0), (80, 0.65, 36), (90, 0.8, 72), (100, 1.0, 72)]
        kept = home_data.thin(samples)
        self.assertEqual((kept[0], kept[-1]), (0, len(samples) - 1))
        self.assertLess(len(kept), len(samples))
        for a, b in zip(kept, kept[1:]):
            (ta, ka, va), (tb, kb, vb) = samples[a], samples[b]
            for t, km, speed in samples[a + 1:b]:
                f = (t - ta) / (tb - ta)
                self.assertLessEqual(abs(ka + (kb - ka) * f - km), 0.02)
                self.assertLessEqual(abs(va + (vb - va) * f - speed), 1.0)
        self.assertEqual(home_data.thin([(0, 0.0, 0), (10, 0.2, 72)]), [0, 1])

    def test_a_run_gives_every_value_of_the_page(self):
        write_run(self.folder / "run")
        out = self.folder / "home-data.json"
        with redirect_stdout(io.StringIO()):
            home_data.main([str(self.folder / "run"), "--out", str(out)])
        values = json.loads(out.read_text(encoding="utf-8"))
        self.assertEqual(sorted(values), sorted(self.committed))
        self.assertEqual({key: values[key] for key in CASE_VALUES}, {key: self.committed[key] for key in CASE_VALUES})
        run = json.loads(values["run_json"])
        self.assertEqual((run["cap"], run["end"], [name for name, _samples in run["tr"]]), (1040, 1025, ["10450-1"]))
        self.assertEqual(values["run_end"], "06:45:25")


if __name__ == "__main__":
    unittest.main()
