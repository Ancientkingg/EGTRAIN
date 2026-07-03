"""
Assignment scene Gvc-Gdg-Ut

Den Haag Centraal - Gouda - Utrecht Centraal corridor.
Contains 2 tracklines (B0 eastbound, B1 westbound), 32 blocks each, 2km per block. B1 uses its own ascending westbound chainage.
8 services: 4 eastbound (Gvc->Gdg->Ut), 4 westbound (Ut->Gdg->Gvc).
"""

import os
import argparse
import shutil
import json

def write_tab_separated(path, rows):
    with open(path, 'w', encoding='utf-8') as f:
        for row in rows:
            f.write('\t'.join(str(c) for c in row) + '\n')

def main():
    parser = argparse.ArgumentParser()
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, '..', '..'))
    default_out = os.path.join(repo_root, 'EGTRAIN', 'QEGTRAIN', 'Scenes', 'Assignment_Gvc_Gdg_Ut')
    
    parser.add_argument('--out', default=default_out, help='Output directory for the generated scene')
    args = parser.parse_args()

    out_dir = args.out

    if os.path.exists(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)

    legacy_dir = os.path.join(out_dir, 'legacy')
    tracklines_dir = os.path.join(legacy_dir, 'TrackLines')
    tms_dir = os.path.join(legacy_dir, 'TMS', 'Timetable Order')
    routes_dir = os.path.join(legacy_dir, 'RoutesToWrite')

    os.makedirs(legacy_dir)
    os.makedirs(tracklines_dir)
    os.makedirs(os.path.join(tracklines_dir, 'B0'))
    os.makedirs(os.path.join(tracklines_dir, 'B1'))
    os.makedirs(tms_dir)
    os.makedirs(routes_dir)

    # legacy/TrackLines/B0 and B1
    for line in ['B0', 'B1']:
        line_dir = os.path.join(tracklines_dir, line)
        
        is_b1 = (line == 'B1')
        offset = 100 if is_b1 else 0
        
        # NodiCumPari.txt
        nodi_rows = []
        for i in range(33):
            nodi_rows.append([i + 1, offset + i * 2, 0])
        write_tab_separated(os.path.join(line_dir, 'NodiCumPari.txt'), nodi_rows)
        
        # ArchiCumPari.txt
        archi_rows = []
        for i in range(1, 33):
            archi_rows.append([100 + i - 1, i, i + 1, "10000.0", "0.0", "36.111111111111"])
        write_tab_separated(os.path.join(line_dir, 'ArchiCumPari.txt'), archi_rows)
        
        # BlockCumPari.txt
        block_rows = []
        for i in range(32):
            block_rows.append([i, 2])
        write_tab_separated(os.path.join(line_dir, 'BlockCumPari.txt'), block_rows)

    # Connections.txt
    with open(os.path.join(tracklines_dir, 'Connections.txt'), 'w', encoding='utf-8') as f:
        pass

    # Stations.txt
    stations_rows = [
        [0, 'Gvc'],
        [28, 'Gdg'],
        [64, 'Ut'],
        [100, 'Ut'],
        [136, 'Gdg'],
        [164, 'Gvc']
    ]
    write_tab_separated(os.path.join(tracklines_dir, 'Stations.txt'), stations_rows)

    # legacy/TMS/Timetable Order/OL0.txt
    with open(os.path.join(tms_dir, 'OL0.txt'), 'w', encoding='utf-8') as f:
        pass
        
    # legacy/RoutesToWrite/RoutesToJoin.txt
    with open(os.path.join(routes_dir, 'RoutesToJoin.txt'), 'w', encoding='utf-8') as f:
        pass

    def write_json(name, data):
        with open(os.path.join(out_dir, name), 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=4)

    # scene.json
    write_json('scene.json', {
        "schema_version": 1,
        "name": "Assignment Gvc-Gdg-Ut",
        "description": "Den Haag Centraal - Gouda - Utrecht Centraal corridor",
        "base_time": "07:00:00",
        "units": {
            "distance": "m",
            "time": "s",
            "speed": "m/s"
        }
    })

    # infrastructure.json
    write_json('infrastructure.json', {
        "nodes": [],
        "arcs": []
    })

    # stations.json
    write_json('stations.json', {
        "stations": [
            {
                "id": "Gvc",
                "name": "Den Haag Centraal",
                "platforms": [{"id": "p1"}, {"id": "p2"}]
            },
            {
                "id": "Gdg",
                "name": "Gouda",
                "platforms": [{"id": "p1"}, {"id": "p2"}]
            },
            {
                "id": "Ut",
                "name": "Utrecht Centraal",
                "platforms": [{"id": "p1"}, {"id": "p2"}]
            }
        ]
    })

    # signalling.json
    route0_blocks = [f"{i}-B0" for i in range(32)]
    route1_blocks = [f"{i}-B1" for i in range(32)]
    
    write_json('signalling.json', {
        "signals": [],
        "routes": [
            {"id": "route0", "blocks": route0_blocks},
            {"id": "route1", "blocks": route1_blocks}
        ]
    })

    # rolling_stock.json
    write_json('rolling_stock.json', {
        "train_units": [
            {
                "id": "SLT_Sprinter",
                "physical": {
                    "mass_of_traction_unit_kg": 151000,
                    "mass_of_a_wagon_kg": 0,
                    "number_of_wagons": 0,
                    "max_speed_ms": 36.111111111111,
                    "max_deceleration_ms2": 0.75,
                    "frontal_area_m2": 1.45,
                    "resistance_coefficient": 0.004,
                    "jerk_ms3": 0.75,
                    "length_m": 70
                },
                "traction_curve": [
                    [0.0, 8.611111111111, 209000.0, 0.0, 0.0],
                    [8.611111111111, 36.111111111111, 324607.2915, -17671.4923, 292.9005]
                ]
            }
        ],
        "compositions": [
            {
                "id": "sprinter_single",
                "units": ["SLT_Sprinter"]
            }
        ]
    })

    # services.json
    services = []
    
    for k in range(4):
        services.append({
            "id": f"svc_e{k+1}",
            "route": "route0",
            "composition": "sprinter_single",
            "entry_time_seconds": 240 + 1800 * k,
            "stops": [
                {"station": "Gvc", "dwell_seconds": 0, "departure_seconds": 300 + 1800 * k},
                {"station": "Gdg", "dwell_seconds": 60, "arrival_seconds": 1260 + 1800 * k, "departure_seconds": 1320 + 1800 * k},
                {"station": "Ut", "dwell_seconds": 0, "arrival_seconds": 2520 + 1800 * k}
            ]
        })
        
    for k in range(4):
        services.append({
            "id": f"svc_w{k+1}",
            "route": "route1",
            "composition": "sprinter_single",
            "entry_time_seconds": 1140 + 1800 * k,
            "stops": [
                {"station": "Ut", "dwell_seconds": 0, "departure_seconds": 1200 + 1800 * k},
                {"station": "Gdg", "dwell_seconds": 60, "arrival_seconds": 2400 + 1800 * k, "departure_seconds": 2460 + 1800 * k},
                {"station": "Gvc", "dwell_seconds": 0, "arrival_seconds": 3420 + 1800 * k}
            ]
        })
        
    write_json('services.json', {"services": services})

    file_count = sum(len(files) for _, _, files in os.walk(out_dir))
    print(f"Generated scene at: {out_dir}")
    print(f"Total files generated: {file_count}")

if __name__ == '__main__':
    main()
