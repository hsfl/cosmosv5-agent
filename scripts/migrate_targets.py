#!/usr/bin/env python3
"""Migrate a realm's legacy targets.dat to the unified JSON layout.

  <realm>/targets.json      array of target shortcuts (see json_target_payload())
  <realm>/nodes/<gs>.json   one NODE_TYPE_GROUNDSTATION node per ground station row

Legacy input is one target per line, either a JSON object (flat latitude/
longitude/altitude, radian rlatitude/rlongitude, or selg/geod "s" blocks) or the
whitespace text form ("name lat lon alt" = ground station, "name lat lon area alt"
= target). Angles in the output are degrees.

Dry run by default: prints what it would write. Pass --write to write. The
original targets.dat is never modified or removed, and existing node files are
never overwritten.

A row whose legacy type is not 1 but which really is a ground station (e.g. a
deep-space tracking station typed as MOC) is named with --groundstation NAME
(repeatable) and is migrated as a ground station node.

usage: migrate_targets.py <realm_dir> [--write] [--groundstation NAME ...]
"""
import json, math, os, re, sys

NODE_TYPE_GROUNDSTATION = 1
NODE_TYPE_MOC = 2
NODE_TYPE_TARGET = 5


def parse_line(line):
    line = line.strip()
    if not line or line.startswith('#'):
        return None
    if line[0] == '{':
        try:
            return json.loads(line)
        except json.JSONDecodeError:
            # Known damage in old files: a stray '}' closing the object early
            try:
                fixed = json.loads(line.replace('},"', ',"', 1))
                print(f"  warning: repaired malformed JSON line: {line[:60]}...")
                return fixed
            except json.JSONDecodeError:
                print(f"  ERROR: unparseable line skipped: {line[:80]}")
                return None
    args = line.split()
    if len(args) == 4:
        return {"name": args[0], "type": NODE_TYPE_GROUNDSTATION,
                "latitude": float(args[1]), "longitude": float(args[2]), "altitude": float(args[3])}
    if len(args) == 5:
        return {"name": args[0], "type": NODE_TYPE_TARGET,
                "latitude": float(args[1]), "longitude": float(args[2]),
                "area": float(args[3]), "altitude": float(args[4])}
    print(f"  ERROR: unsupported text line ({len(args)} fields) skipped: {line[:80]}")
    return None


def to_shortcut(row):
    """Return the shortcut dict for one legacy row (body/degrees normalized)."""
    out = {"name": row.get("name", ""), "type": row.get("type", 0)}
    if "selg" in row and "s" in row["selg"]:
        s, out["body"] = row["selg"]["s"], "moon"
        lat, lon, h = s.get("lat", 0.), s.get("lon", 0.), s.get("h", 0.)
    elif "geod" in row and "s" in row["geod"]:
        s, out["body"] = row["geod"]["s"], "earth"
        lat, lon, h = s.get("lat", 0.), s.get("lon", 0.), s.get("h", 0.)
    else:
        out["body"] = "earth"
        lat = math.degrees(row["rlatitude"]) if "rlatitude" in row else row.get("latitude", 0.)
        lon = math.degrees(row["rlongitude"]) if "rlongitude" in row else row.get("longitude", 0.)
        h = row.get("altitude", 0.)
    out.update({"lat": lat, "lon": lon, "h": h})
    if "radius" in row:
        out["radius"] = row["radius"]
    elif "area" in row:
        out["area"] = row["area"]
    if "minelto" in row:
        out["minel"] = math.degrees(row["minelto"])
    return out


def gs_node(sc):
    """Minimal canonical node payload for a ground station (see json_node_payload())."""
    lat, lon = math.radians(sc["lat"]), math.radians(sc["lon"])
    geod = {"utc": 0.0, "s": {"lat": lat, "lon": lon, "h": sc["h"]}, "pass": 1}
    return {"node": {"name": sc["name"], "type": NODE_TYPE_GROUNDSTATION,
                     "loc": {"utc": 0.0, "pos": {"geod": geod}}}}


def main():
    argv = sys.argv[1:]
    force_gs = set()
    while '--groundstation' in argv:
        i = argv.index('--groundstation')
        force_gs.add(argv[i + 1])
        del argv[i:i + 2]
    args = [a for a in argv if not a.startswith('--')]
    write = '--write' in argv
    if len(args) != 1:
        print(__doc__); return 2
    realm = args[0]
    src = os.path.join(realm, "targets.dat")
    if not os.path.isfile(src):
        print(f"no {src}"); return 1

    targets, stations = [], []
    with open(src) as f:
        for line in f:
            row = parse_line(line)
            if row is None:
                continue
            sc = to_shortcut(row)
            if sc["name"] in force_gs:
                sc["type"] = NODE_TYPE_GROUNDSTATION
            if sc["type"] == NODE_TYPE_GROUNDSTATION:
                stations.append(sc)
            else:
                if sc["type"] == NODE_TYPE_MOC:
                    print(f"  warning: '{sc['name']}' has type 2 (MOC); MOCs are logical nodes, "
                          "not targets. Kept in targets.json for you to reclassify.")
                targets.append(sc)

    print(f"{realm}: {len(targets)} target(s), {len(stations)} ground station node(s)")
    tjson = os.path.join(realm, "targets.json")
    text = "[\n" + ",\n".join("  " + json.dumps(t) for t in targets) + "\n]\n"
    print(f"  -> {tjson}"); 
    for sc in stations:
        print(f"  -> {os.path.join(realm, 'nodes', sc['name'] + '.json')}")
    if not write:
        print("dry run; pass --write to write"); return 0

    with open(tjson, "w") as f:
        f.write(text)
    os.makedirs(os.path.join(realm, "nodes"), exist_ok=True)
    for sc in stations:
        path = os.path.join(realm, "nodes", sc["name"] + ".json")
        if os.path.exists(path):
            print(f"  skip (exists): {path}"); continue
        with open(path, "w") as f:
            json.dump(gs_node(sc), f)
    return 0


if __name__ == "__main__":
    sys.exit(main())
