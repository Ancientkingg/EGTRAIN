#!/usr/bin/env python3
from pathlib import Path


SOURCE = Path(__file__).resolve().parents[2] / "EGTRAIN/QEGTRAIN/simulation/Signalling.cpp"


def main() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    if "i <= N_BLSPrev_Conn" in text:
        raise SystemExit("connected-block cleanup reads one entry past the initialized range")


if __name__ == "__main__":
    main()
