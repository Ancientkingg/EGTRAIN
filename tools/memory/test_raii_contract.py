#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    simulation = (ROOT / "EGTRAIN/QEGTRAIN/simulation/Simulation.cpp").read_text()
    dispatch_controller = (ROOT / "EGTRAIN/QEGTRAIN/app/DispatchController.cpp").read_text()

    delay_functions = simulation[: simulation.index("void calculateDelayStatsForAllStations")]
    station_delay_functions = simulation[
        simulation.index("void calculateStationDelayStatistics") : simulation.index("} // namespace")
    ]
    for name in ("arrivals", "consecutive"):
        if station_delay_functions.count(f"{name}.push_back(") != 1:
            raise SystemExit(f"{name} must grow at every recorded stop")
    if re.search(r"new\s+double\s*\[\s*numRegions\s*\]", delay_functions):
        raise SystemExit("delay statistics still use owning raw arrays")
    if re.search(r"delete\s*\[\s*\]\s*(?:TrainDelay|TrainConsDelay|TrainEntDelay|Disturb)", delay_functions):
        raise SystemExit("delay statistics still delete raw arrays")
    expected_vectors = {"delays": 1}
    for name, count in expected_vectors.items():
        pattern = rf"std::vector<double>\s+{name}\s*\(\s*numRegions\s*\)"
        if len(re.findall(pattern, delay_functions)) != count:
            raise SystemExit(f"{name} must use {count} numRegions-sized vectors")

    if "void DispatchController::setupEgtrain()" in dispatch_controller:
        raise SystemExit("obsolete legacy setup path is still compiled")
    if "void DispatchController::prepareSimulation()" in dispatch_controller:
        raise SystemExit("obsolete legacy preparation path is still compiled")


if __name__ == "__main__":
    main()
