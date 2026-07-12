#!/usr/bin/env python3
from pathlib import Path


SOURCE = Path(__file__).resolve().parents[2] / "EGTRAIN/QEGTRAIN/simulation/Signalling.cpp"


def main() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    if "i <= N_BLSPrev_Conn" in text:
        raise SystemExit("connected-block cleanup reads one entry past the initialized range")
    dependencies = text[
        text.index("void setDependenciesBetweenBlocks()") : text.index("bool areBlocksConnected")
    ]
    if "N_ConnectedBS >= maxConnectedBlocks" not in dependencies:
        raise SystemExit("connected-block insertion does not enforce the IDConnectedBS capacity")


if __name__ == "__main__":
    main()
