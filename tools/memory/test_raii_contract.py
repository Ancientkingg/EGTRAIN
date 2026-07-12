#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    simulation = (ROOT / "EGTRAIN/QEGTRAIN/simulation/Simulation.cpp").read_text()
    rescheduling_h = (ROOT / "EGTRAIN/QEGTRAIN/simulation/Rescheduling.h").read_text()
    rescheduling_cpp = (ROOT / "EGTRAIN/QEGTRAIN/simulation/Rescheduling.cpp").read_text()

    delay_functions = simulation[: simulation.index("void calculateDelayStatsForAllStations")]
    if re.search(r"new\s+double\s*\[\s*numRegions\s*\]", delay_functions):
        raise SystemExit("delay statistics still use owning raw arrays")
    if re.search(r"delete\s*\[\s*\]\s*(?:TrainDelay|TrainConsDelay|TrainEntDelay|Disturb)", delay_functions):
        raise SystemExit("delay statistics still delete raw arrays")
    expected_vectors = {
        "TrainDelay": 3,
        "TrainConsDelay": 2,
        "TrainEntDelay": 1,
        "Disturb": 1,
    }
    for name, count in expected_vectors.items():
        pattern = rf"std::vector<double>\s+{name}\s*\(\s*numRegions\s*\)"
        if len(re.findall(pattern, delay_functions)) != count:
            raise SystemExit(f"{name} must use {count} numRegions-sized vectors")
    if "extern std::unique_ptr<Rescheduling> dispatchingTool;" not in rescheduling_h:
        raise SystemExit("dispatchingTool is not declared as a unique_ptr")
    if "std::make_unique<Rescheduling>()" not in rescheduling_cpp:
        raise SystemExit("dispatchingTool is not created with make_unique")
    if "Rescheduling* dispatchingTool = new" in rescheduling_cpp:
        raise SystemExit("dispatchingTool still uses raw ownership")


if __name__ == "__main__":
    main()
