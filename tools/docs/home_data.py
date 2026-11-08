#!/usr/bin/env python3
"""Writes docs/site/home-data.json: the train graph and the playback of a run on the home page of the website.

usage: home_data.py RUN [--out FILE]

The train graph shows the planned stops of the services of the bundled case Milano_Brescia that stop at two stations or
more. The playback shows the trains and the reserved sections of track of a run of that case, at the moment of the
screenshot docs/images/readme-main-window.png (06:45:40) and, with the script of the page, at any moment of the run.
The script reads the case from EGTRAIN/QEGTRAIN/Scenes/Milano_Brescia. RUN is the output folder of a run of it without a
window and with detailed trajectories: the script reads its per-train files TEMP/Traj_Train_*.txt and
TrainTrajectories/BlockingTimes.txt. Make the run and the file from the root of the repository, with the program of a
build from source (build/QEGTRAIN on Linux):

  cmake -S . -B build
  cmake --build build --target QEGTRAIN
  QEGTRAIN_OUTPUT_DIR="$TMPDIR/egtrain-run" build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN \\
      --scene EGTRAIN/QEGTRAIN/Scenes/Milano_Brescia -g 0 --detailed-trajectories
  python3 tools/docs/home_data.py "$TMPDIR/egtrain-run/Output/Milano_Brescia"

The run uses the default seed, so the same case and program give the same file. Commit the file: tools/docs/home_page.py
puts its values in place of the {name} of the same name in docs/site/index.html, and the publication runs no
simulation.

Of each train the script takes the first and the last sample and those at every 10 s of the run. A sample is drawn on
the drawing level of the last track that its block names, the track that the train runs onto at a switch, and a reserved
section on the level of the first. A sample is left out when the straight line between the samples kept around it gives
its km within 0.02 km and its speed within 1 km/h, so the page stays small. Each stretch of a train on one track is
thinned on its own, so a change of track keeps its time.
"""
import argparse
import html
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CASE = ROOT / "EGTRAIN/QEGTRAIN/Scenes/Milano_Brescia"
OUT = ROOT / "docs/site/home-data.json"
CAPTURE_TIME = "06:45:40"  # the moment of docs/images/readme-main-window.png
TRACK = re.compile(r"@\d+-([^@]+)@")
esc = html.escape

# Station names as the page writes them.
NAMES = {"PioltelloLim.": "Pioltello Lim.", "PioltelloLim.1Tr": "Pioltello Lim. 1Tr", "CassanoD'Adda": "Cassano D'Adda",
         "MorengoBariano": "Morengo Bariano", "OspitalettoT.": "Ospitaletto T.", "BivioRoncadelle": "Bivio Roncadelle"}


def name(station: str) -> str:
    return NAMES.get(station, station)


def seconds(clock_time: str) -> int:
    hours, minutes, secs = (int(part) for part in clock_time.split(":"))
    return hours * 3600 + minutes * 60 + secs


def union(spans):
    """The spans as a sorted list of [from, to] in which no two overlap or touch."""
    spans = sorted(spans)
    out = [list(spans[0])]
    for a, b in spans[1:]:
        if a <= out[-1][1] + 1e-6:
            out[-1][1] = max(out[-1][1], b)
        else:
            out.append([a, b])
    return out


def read_case(folder: Path) -> dict:
    """The parts of the case that the page draws: its tracks with their drawing level and km, the crossovers, the
    stations and the services that stop at two stations or more, with their planned times in seconds after base_time."""
    def load(file_name):
        return json.loads((folder / file_name).read_text(encoding="utf-8"))

    infrastructure, views = load("infrastructure.json"), load("views.json")
    level = {track["track"]: track["level"] for track in views["tracks"]}
    nodes = {node["id"]: node for node in infrastructure["nodes"]}
    spans = {}
    for arc in infrastructure["arcs"]:
        a, b = nodes[arc["from"]]["x_km"], nodes[arc["to"]]["x_km"]
        spans.setdefault(arc["track"], []).append((min(a, b), max(a, b)))
    km = [node["x_km"] for node in infrastructure["nodes"]]

    def end(node_id):
        return nodes[node_id]["x_km"], level[nodes[node_id]["track"]]

    return {
        "base_time": load("scene.json")["base_time"],
        "km_range": (min(km), max(km)),
        "level": level,
        "tracks": [(track["track"], track["level"], union(spans[track["track"]])) for track in views["tracks"]],
        "connections": [(*end(c["from"]), *end(c["to"])) for c in infrastructure["connections"]],
        "stations": [(station["name"], station["position_km"]) for station in load("stations.json")["stations"]],
        "services": [(service["id"], [(stop["station"], stop.get("planned_arrival_seconds"), stop.get("planned_departure_seconds"))
                                      for stop in service["stops"]])
                     for service in load("services.json")["services"] if len(service["stops"]) >= 2],
    }


def read_run(folder: Path, level: dict) -> tuple:
    """The trains of the run, each a name and samples of time, km, drawing level and speed in km/h, and the reserved
    sections of each train: km from and to, the times it holds them and the drawing level."""
    trains = []
    for path in sorted((folder / "TEMP").glob("Traj_Train_*.txt")):
        rows = [line.split("\t") for line in path.read_text(encoding="utf-8").splitlines()[1:] if line.strip()]
        samples = [[int(float(row[0])), round(float(row[2]) / 1000, 3), level[TRACK.findall(row[7])[-1]], round(float(row[1]) * 3.6)]
                   for index, row in enumerate(rows) if index in (0, len(rows) - 1) or int(float(row[0])) % 10 == 0]
        trains.append((path.name[len("Traj_Train_"):-len(".txt")], samples))
    lines = (folder / "TrainTrajectories/BlockingTimes.txt").read_text(encoding="utf-8").splitlines()
    blocking = []
    for first in range(0, len(lines) - 5, 6):
        blocks, starts, ends, begins, finishes = (lines[first + k].split() for k in range(1, 6))
        blocking.append([[round(float(a) / 1000, 3), round(float(b) / 1000, 3), float(c), float(d), level[TRACK.match(block).group(1)]]
                         for block, a, b, c, d in zip(blocks, starts, ends, begins, finishes)])
    return trains, blocking


def thin(s, tol_km=0.02, tol_v=1.0):
    """Douglas-Peucker on time, over (time, km, speed) points; returns the indices kept. A sample is dropped only when
    the straight line between the kept samples around it gives its km within tol_km and its speed within tol_v.
    Stops and waits stay, because the speed changes there."""
    if len(s) < 3:
        return list(range(len(s)))
    keep = [False] * len(s)
    keep[0] = keep[-1] = True
    stack = [(0, len(s) - 1)]
    while stack:
        a, b = stack.pop()
        if b - a < 2:
            continue
        (ta, ka, va), (tb, kb, vb) = s[a], s[b]
        worst, wi = 1.0, None
        for i in range(a + 1, b):
            t, k, v = s[i]
            f = (t - ta) / (tb - ta)
            e = max(abs(ka + (kb - ka) * f - k) / tol_km, abs(va + (vb - va) * f - v) / tol_v)
            if e > worst:
                worst, wi = e, i
        if wi is not None:
            keep[wi] = True
            stack += [(a, wi), (wi, b)]
    return [i for i, k in enumerate(keep) if k]


def graph(case: dict) -> dict:
    """The train graph: time across from 05:45 to 08:45, the stations down at their km, a path for each service."""
    base = seconds(case["base_time"])
    cap = seconds(CAPTURE_TIME) - base
    km = {station: kmv for station, kmv in case["stations"]}

    def clock(t):
        s = int(round(base + t))
        return f"{s // 3600:02d}:{s % 3600 // 60:02d}:{s % 60:02d}"

    t0, t1 = seconds("05:45:00") - base, seconds("08:45:00") - base
    k0, k1 = 9.0, 79.3

    def X(t):
        return round((t - t0) / (t1 - t0) * 1000, 2)

    def Y(kmv):
        return round((kmv - k0) / (k1 - k0) * 1000, 2)

    services = []
    for sid, stops_ in case["services"]:
        group = "teal" if sid.startswith(("23", "106")) else "ink" if sid.startswith("107") else "red"
        pts, stops = [], []
        for station, arrive, depart in stops_:
            y = Y(km[station])
            if arrive is not None:
                pts.append((X(arrive), y))
            if depart is not None:
                pts.append((X(depart), y))
            stops.append([name(station), km[station], arrive, depart])
        first, last = stops_[0], stops_[-1]
        t_first = first[2] if first[2] is not None else first[1]
        t_last = last[1] if last[1] is not None else last[2]
        start = min(p for stop in stops_ for p in stop[1:] if p is not None)
        services.append(dict(id=sid, group=group, d="M" + " L".join(f"{x} {y}" for x, y in pts), start=start, stops=stops,
                             summary=f"Service {sid}, from {name(first[0])} at {clock(t_first)} "
                                     f"to {name(last[0])} at {clock(t_last)}, {len(stops)} stops"))
    services.sort(key=lambda s: (s["start"], s["id"]))

    stop_stations = {stop[0] for s in services for stop in s["stops"]}
    labelled = ["Segrate", "PioltelloLim.", "Vignate", "Melzo", "CassanoD'Adda", "Treviglio", "Vidalengo", "MorengoBariano",
                "Romano", "Calcio", "Chiari", "Rovato", "OspitalettoT.", "BivioRoncadelle"]
    nudge = {"Segrate": -2, "PioltelloLim.": 2}   # px, keeps the two closest labels apart at 13 px and more

    grid = []
    for c in range(20700 + 600 - 20700 % 600, 31500, 600):
        cls = "h" if c % 3600 == 0 else "m" if c % 1800 == 0 else "n"
        x = X(c - base)
        grid.append(f'<line class="v {cls}" x1="{x}" x2="{x}" y1="0" y2="1000"/>')
    for station, kmv in case["stations"]:
        cls = "stop" if name(station) in stop_stations or station == "BivioRoncadelle" else "pass"
        y = Y(kmv)
        grid.append(f'<line class="st {cls}" x1="0" x2="1000" y1="{y}" y2="{y}"/>')
    xm = X(cap)
    grid.append(f'<line class="mark" x1="{xm}" x2="{xm}" y1="0" y2="1000"/>')

    paths = [f'<g class="svc {s["group"]}" data-i="{i}" role="option" aria-selected="false" '
             f'tabindex="{0 if i == 0 else -1}" aria-label="{esc(s["summary"])}"><path class="hit" d="{s["d"]}"/>'
             f'<path class="line" d="{s["d"]}"/></g>' for i, s in enumerate(services)]

    axis = [f'<span class="tick" style="left:{X(c - base) / 10}%">{c // 3600:02d}:{c % 3600 // 60:02d}</span>'
            for c in range(21600, 31500, 1800)]
    axis.append(f'<span class="tick at" style="left:{xm / 10}%">{clock(cap)}</span>')

    labels = [f'<li style="--y:{Y(kmv) / 10}%;--dy:{nudge.get(station, 0)}px">'
              f'<span class="name">{esc(name(station))}</span><span class="km">{kmv:.1f}</span></li>'
              for station, kmv in case["stations"] if station in labelled]

    def t_cell(t):
        if t is None:
            return '<td class="t none"><span aria-hidden="true">&ndash;</span><span class="vh">none</span></td>'
        return f'<td class="t">{clock(t)}</td>'

    # The graph as a table: one row group per service, in the order the services start.
    tbodies = []
    for s in services:
        rows = []
        for k, (station, kmv, arrive, depart) in enumerate(s["stops"]):
            head = f'<th scope="rowgroup" rowspan="{len(s["stops"])}" class="svc-h">{s["id"]}</th>' if k == 0 else ""
            rows.append(f'<tr>{head}<td>{esc(station)}</td><td class="n">{kmv:.1f}</td>{t_cell(arrive)}{t_cell(depart)}</tr>')
        tbodies.append("<tbody>" + "".join(rows) + "</tbody>")

    data = {"t0": t0, "t1": t1, "base": base, "k0": k0, "k1": k1,
            "svc": [{"id": s["id"], "g": s["group"], "stops": s["stops"]} for s in services]}
    first_station, last_station = case["stations"][0], case["stations"][-1]
    return {
        "graph_grid": "\n          ".join(grid),
        "graph_paths": "\n            ".join(paths),
        "graph_axis": "\n              ".join(axis),
        "graph_labels": "\n              ".join(labels),
        "graph_table": "\n                ".join(tbodies),
        "graph_json": json.dumps(data, separators=(",", ":")),
        "graph_trev": f'{Y(km["Treviglio"]) / 1000:.4f}',
        "graph_osp": f'{Y(km["OspitalettoT."]) / 1000:.4f}',
        "graph_br": f'{Y(km["BivioRoncadelle"]) / 1000:.4f}',
        "graph_mark": f"{xm / 10:.3f}",
        "services": str(len(services)),
        "capture": clock(cap),
        "first_station": name(first_station[0]),
        "first_km": f"{first_station[1]:.1f}",
        "last_station": name(last_station[0]),
        "last_km": f"{last_station[1]:.1f}",
    }


def drawing(case: dict, trains: list, blocking: list) -> dict:
    """The playback: a line drawing of the tracks by drawing level and of the stations by km, with the trains of the run
    and the sections of track reserved for them, drawn at 06:45:40, and the run for the script of the page."""
    base = seconds(case["base_time"])
    cap = seconds(CAPTURE_TIME) - base

    def clock(t):
        s = int(round(base + t))
        return f"{s // 3600:02d}:{s % 3600 // 60:02d}:{s % 60:02d}"

    # SVG units are CSS px at 1440 wide. The drawing never shows narrower than MIN_W px (it scrolls in its own box
    # instead), so FS units of text render at FS * MIN_W / W >= 13 px.
    W, PADL, PADR = 1360, 24, 24
    MIN_W, FS = 1105, 16
    assert FS * MIN_W / W >= 13, FS * MIN_W / W
    DK0, DK1 = case["km_range"]
    TOP, GAP, LMAX = 30, 6.0, 21
    STN_Y = round(TOP + LMAX * GAP + 22)
    SCALE_Y = STN_Y + 36
    H = SCALE_Y + 32
    labelled = ("Segrate", "Vignate", "Melzo", "CassanoD'Adda", "Treviglio", "Caravaggio", "Romano", "Calcio", "Chiari",
                "Coccaglio", "Rovato", "BivioRoncadelle")

    def DX(kmv):
        return round(PADL + (kmv - DK0) / (DK1 - DK0) * (W - PADL - PADR), 1)

    def DY(level):
        # Higher drawing levels higher up, as the application draws them.
        return round(TOP + (LMAX - level) * GAP, 1)

    def connection_d(xa, ya, xb, yb):
        """A crossover as a slanted stroke centred on its real position, at 45 degrees."""
        dy = abs(yb - ya)
        if abs(xb - xa) < dy:
            mid, s = (xa + xb) / 2, 1 if xb >= xa else -1
            xa, xb = mid - s * dy / 2, mid + s * dy / 2
        return f"M{xa:.1f} {ya:.1f}L{xb:.1f} {yb:.1f}"

    by_level = {}
    for _track, level, spans in case["tracks"]:
        by_level.setdefault(level, []).extend(spans)
    SPANS = {lv: union(s) for lv, s in by_level.items()}
    TRACK_D = "".join(f"M{DX(a):.1f} {DY(lv):.1f}H{DX(b):.1f}" for lv, sp in sorted(SPANS.items()) for a, b in sp)
    TRACK_D += "".join(connection_d(DX(fk), DY(fl), DX(tk), DY(tl)) for fk, fl, tk, tl in case["connections"])
    TRACKS = case["tracks"]

    def covered(level, kmv, tol=0.03):
        return any(a - tol <= kmv <= b + tol for a, b in SPANS.get(level, []))

    def track_at(level, kmv):
        """Index of the track on this drawing level whose span is nearest to kmv."""
        best, bd = None, 1e9
        for i, (_, lv, spans) in enumerate(TRACKS):
            if lv != level:
                continue
            for a, b in spans:
                dd = 0 if a <= kmv <= b else min(abs(kmv - a), abs(kmv - b))
                if dd < bd:
                    best, bd = i, dd
        return best

    # Stations: a tick for each, a name for the main ones where it fits.
    stn = [f'<line x1="{PADL - 8}" y1="{STN_Y}" x2="{W - PADR + 8}" y2="{STN_Y}"/>']
    texts, last_r = [], -1e9
    for _station, kmv in case["stations"]:
        stn.append(f'<line x1="{DX(kmv)}" y1="{STN_Y}" x2="{DX(kmv)}" y2="{STN_Y + 7}"/>')
    for station, kmv in case["stations"]:
        if station not in labelled:
            continue
        x, w = DX(kmv), len(name(station)) * 0.52 * FS + 4
        anchor, left, right = "", x - w / 2, x + w / 2
        if left < 0:
            anchor, left, right = ' class="s"', x - 2, x - 2 + w
        elif right > W:
            anchor, left, right = ' class="e"', x - w + 2, x + 2
        if left >= last_r + 10:
            texts.append(f'<text x="{x}" y="{STN_Y + 25}"{anchor}>{esc(name(station))}</text>')
            last_r = right

    sc = [f'<line x1="{PADL - 8}" y1="{SCALE_Y}" x2="{W - PADR + 8}" y2="{SCALE_Y}"/>']
    for k in range(10, 79, 5):
        x = DX(k)
        sc.append(f'<line x1="{x}" y1="{SCALE_Y}" x2="{x}" y2="{SCALE_Y + (8 if k % 10 == 0 else 5)}"/>')
        if k % 10 == 0:
            sc.append(f'<text x="{x}" y="{SCALE_Y + 25}">{k}</text>')
    sc.append(f'<text x="{W - 2}" y="{SCALE_Y + 25}" class="e">km</text>')

    # Each sample: time, km, the track on its drawing level nearest to that km, speed. A sample keeps its previous
    # drawing level while its reported level has no track at that km (the run reports the next track a little early,
    # before the train reaches the crossover). The samples are thinned between track changes, so every change of track
    # keeps its two raw samples and its timing.
    d_trains, n_raw, n_kept = [], 0, 0
    for train, s in trains:
        if not s:
            continue
        seq, cur = [], None
        for t, kmv, lv, v in s:
            if cur is None or covered(lv, kmv) or not covered(cur, kmv):
                cur = lv
            seq.append((t, kmv, track_at(cur, kmv), v))
        segs = [[seq[0]]]
        for p in seq[1:]:
            if p[2] != segs[-1][-1][2]:
                segs.append([p])
            else:
                segs[-1].append(p)
        kept = [seg[i] for seg in segs for i in thin([(q[0], q[1], q[3]) for q in seg])]
        n_raw += len(seq)
        n_kept += len(kept)
        flat = []
        for t, kmv, ti, v in kept:
            flat += [int(t), int(round(kmv * 100)), ti, int(round(v))]
        d_trains.append([train, flat])

    d_blocks = []
    for blocks in blocking:
        for k1, k2, b0, b1, lv in blocks:
            a, z = min(k1, k2), max(k1, k2)
            for sa, sb in SPANS.get(lv, []):
                lo, hi = max(a, sa), min(z, sb)
                if hi - lo > 0.01:
                    d_blocks += [int(round(lo * 100)), int(round(hi * 100)), lv, int(b0 // 1), int(-(-b1 // 1))]
    run_end = max(s[-1][0] for _train, s in trains if s)  # the last sample of the run

    def deltas(flat):
        """Time and km as differences from the previous point, to keep the page small. The script adds them up again."""
        out, pt, pk = [], 0, 0
        for i in range(0, len(flat), 4):
            t, k, ti, v = flat[i:i + 4]
            out += [t - pt, k - pk, ti, v]
            pt, pk = t, k
        return out

    enc_blocks = []
    for i in range(0, len(d_blocks), 5):
        lo, hi, lv, b0, b1 = d_blocks[i:i + 5]
        enc_blocks += [lo, hi - lo, lv, b0, b1 - b0]
    run_json = json.dumps({"geo": [W, PADL, PADR, DK0, DK1, TOP, GAP, LMAX], "cap": cap, "end": run_end, "base": base,
                           "tk": [[n, lv] for n, lv, _ in TRACKS], "tr": [[n, deltas(f)] for n, f in d_trains], "bl": enc_blocks},
                          separators=(",", ":")).replace("</", "<\\/")

    # The state at 06:45:40, drawn into the page so that it shows without the script too.
    def state(flat, t):
        T = flat[0::4]
        if t < T[0] or t > T[-1]:
            return None
        lo = max(i for i in range(len(T)) if T[i] <= t)
        hi = min(lo + 1, len(T) - 1)
        f = (t - T[lo]) / (T[hi] - T[lo]) if T[hi] > T[lo] else 0

        def k(i):
            return flat[4 * i + 1] / 100

        kmv = k(lo) + (k(hi) - k(lo)) * f
        yl, yh = DY(TRACKS[flat[4 * lo + 2]][1]), DY(TRACKS[flat[4 * hi + 2]][1])
        v = round(flat[4 * lo + 3] + (flat[4 * hi + 3] - flat[4 * lo + 3]) * f)
        dirn = 1
        for i in range(lo, -1, -1):
            nxt = k(i + 1) - k(i) if i + 1 < len(T) else 0
            if abs(nxt) > 0.000005:
                dirn = 1 if nxt > 0 else -1
                break
        return kmv, yl + (yh - yl) * f, v, dirn, flat[4 * (lo if f < 0.5 else hi) + 2]

    now_marks, now_rows = [], []
    for nm, flat in d_trains:
        st = state(flat, cap)
        if not st:
            continue
        kmv, y, v, dirn, ti = st
        flip = " scale(-1 1)" if dirn < 0 else ""
        now_marks.append(f'<path class="tr" d="M-20 -5H-7L0 0L-7 5H-20Z" transform="translate({DX(kmv):.1f} {y:.1f}){flip}"/>')
        now_rows.append((kmv, nm, TRACKS[ti][0], v))
    now_rows.sort()

    # Ticks under the time slider, as a fraction of the run.
    ticks = "".join(f'<span style="--x:{(c - base) / run_end:.4f}"{cls}>{c // 3600:02d}:{c % 3600 // 60:02d}</span>'
                    for c, cls in ((23400, ""), (25200, ""), (26100, ' class="mid"'), (27000, "")))
    ticks += f'<span class="at" style="--x:{cap / run_end:.4f}">{clock(cap)}</span>'

    values = {
        "capture_seconds": str(cap),
        "capture_fraction": f"{cap / run_end:.4f}",
        "run_seconds": str(run_end),
        "run_end": clock(run_end),
        "km_from": f"{DK0:.1f}",
        "km_to": f"{DK1:.1f}",
        "stations": str(len(case["stations"])),
        "drawing_width": str(W),
        "drawing_height": str(H),
        "drawing_min_width": str(MIN_W),
        "drawing_font": str(FS),
        "drawing_centre": str(DX(dict(case["stations"])["Treviglio"])),
        "drawing_tracks": TRACK_D,
        "drawing_stations": "".join(stn) + '<g class="lab">' + "".join(texts) + "</g>",
        "drawing_scale": "".join(sc),
        "drawing_trains": "".join(now_marks),
        "drawing_blocks": "".join(f"M{DX(d_blocks[i] / 100):.1f} {DY(d_blocks[i + 2]):.1f}H{DX(d_blocks[i + 1] / 100):.1f}"
                                  for i in range(0, len(d_blocks), 5) if d_blocks[i + 3] <= cap <= d_blocks[i + 4]),
        "train_rows": "".join(f'<tr><th scope="row">{n}</th><td class="n">{k:.1f}</td><td>{tk}</td><td class="n">{v}</td></tr>'
                              for k, n, tk, v in now_rows),
        "train_caption": f"{len(now_rows)} trains on the line at {clock(cap)}",
        "station_rows": "".join(f'<tr><th scope="row">{esc(name(station))}</th><td class="n">{kmv}</td></tr>'
                                for station, kmv in case["stations"]),
        "scrub_ticks": ticks,
        "run_json": run_json,
    }
    summary = (f"run: {len(d_trains)} trains, {n_kept} of {n_raw} samples kept, {len(d_blocks) // 5} reserved sections, "
               f"run data {len(run_json)} bytes; {len(now_rows)} trains at {clock(cap)}; drawing {W} x {H}, "
               f"{len(texts)} station names")
    return values, summary


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run", type=Path, help="the output folder of the run, such as Output/Milano_Brescia")
    parser.add_argument("--out", type=Path, default=OUT, help="the file to write")
    arguments = parser.parse_args(argv)
    case = read_case(CASE)
    trains, blocking = read_run(arguments.run, case["level"])
    if not any(samples for _train, samples in trains):
        raise SystemExit(f"{arguments.run} holds no trajectory: run the case with --detailed-trajectories")
    values = graph(case)
    run_values, summary = drawing(case, trains, blocking)
    values.update(run_values)
    arguments.out.write_text(json.dumps(values, indent=1) + "\n", encoding="utf-8")
    print(f"{arguments.out}: {values['services']} services; {summary}")


if __name__ == "__main__":
    main()
