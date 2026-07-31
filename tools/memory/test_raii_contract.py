#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    simulation = (ROOT / "EGTRAIN/QEGTRAIN/simulation/Simulation.cpp").read_text()
    dispatch_controller = (ROOT / "EGTRAIN/QEGTRAIN/app/DispatchController.cpp").read_text()

    delay_functions = simulation[: simulation.index("void calculateDelayStatsForAllStations")]
    station_delay_functions = simulation[
        simulation.index("void calculateDelayStatsAtStation") : simulation.index("void Compute_Input_Delays")
    ]
    for name in ("TrainDelay", "TrainConsDelay"):
        if station_delay_functions.count(f"{name}.push_back(") != 4:
            raise SystemExit(f"{name} must grow at every matching stop")
    if re.search(r"new\s+double\s*\[\s*numRegions\s*\]", delay_functions):
        raise SystemExit("delay statistics still use owning raw arrays")
    if re.search(r"delete\s*\[\s*\]\s*(?:TrainDelay|TrainConsDelay|TrainEntDelay|Disturb)", delay_functions):
        raise SystemExit("delay statistics still delete raw arrays")
    expected_vectors = {
        "TrainDelay": 1,
        "TrainConsDelay": 0,
        "TrainEntDelay": 1,
        "Disturb": 1,
    }
    for name, count in expected_vectors.items():
        pattern = rf"std::vector<double>\s+{name}\s*\(\s*numRegions\s*\)"
        if len(re.findall(pattern, delay_functions)) != count:
            raise SystemExit(f"{name} must use {count} numRegions-sized vectors")

    setup = dispatch_controller[
        dispatch_controller.index("void DispatchController::setupEgtrain()") :
        dispatch_controller.index("void DispatchController::resetState()")
    ]
    output_initialization = (
        'std::ofstream outputFile(InputMainFolder + "/Rescheduling/EGTRAINOutput.txt", '
        "std::ios::binary | std::ios::trunc);"
    )
    if output_initialization not in setup:
        raise SystemExit("EGTRAINOutput.txt must be truncated directly during setup")


if __name__ == "__main__":
    main()
