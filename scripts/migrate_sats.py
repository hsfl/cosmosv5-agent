#!/usr/bin/env python3
"""Move each satellite's structural type from sats*.dat into its node file.

The Simulator now takes a node's structural type ("U12XY", ...) from
node.stype in <realm>/nodes/<nodename>.json. For every JSON line in the realm's
sats*.dat that has both "nodename" and "type", this writes (or patches) that
node file:

  * no node file        -> a minimal one: {"node": {"name", "type", "stype"}}
  * node file, no stype -> stype is added; everything else is left alone
  * node file with stype -> left alone (a differing sats.dat type is reported)

sats.dat itself is never modified (its "type" is deprecated but still read as a
fallback). Dry run by default.

usage: migrate_sats.py <realm_dir> [--write]
"""
import glob, json, os, sys

NODE_TYPE_SATELLITE = 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    write = '--write' in sys.argv
    if len(args) != 1:
        print(__doc__); return 2
    realm = args[0]

    wanted = {}  # nodename -> (stype, nodetype)
    for path in sorted(glob.glob(os.path.join(realm, "sats*.dat"))):
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line.startswith('{'):
                    continue
                try:
                    row = json.loads(line)
                except json.JSONDecodeError:
                    print(f"  ERROR: unparseable line in {os.path.basename(path)} skipped: {line[:70]}")
                    continue
                if "nodename" not in row or "type" not in row:
                    continue
                name, stype = row["nodename"], row["type"]
                nodetype = row.get("nodetype", NODE_TYPE_SATELLITE)
                if name in wanted and wanted[name][0] != stype:
                    print(f"  warning: '{name}' has differing types across sats files "
                          f"({wanted[name][0]} vs {stype} in {os.path.basename(path)}); keeping the first")
                    continue
                wanted.setdefault(name, (stype, nodetype))

    nodes = os.path.join(realm, "nodes")
    for name, (stype, nodetype) in sorted(wanted.items()):
        path = os.path.join(nodes, name + ".json")
        if os.path.exists(path):
            with open(path) as f:
                payload = json.load(f)
            existing = payload.get("node", {}).get("stype", "")
            if existing:
                note = "" if existing == stype else f" (sats.dat says {stype}; file wins)"
                print(f"  keep   {path}: stype {existing}{note}")
                continue
            payload.setdefault("node", {})["stype"] = stype
            print(f"  patch  {path}: add stype {stype}")
        else:
            payload = {"node": {"name": name, "type": nodetype, "stype": stype}}
            print(f"  create {path}: stype {stype}")
        if write:
            os.makedirs(nodes, exist_ok=True)
            with open(path, "w") as f:
                json.dump(payload, f)
    if not write:
        print("dry run; pass --write to write")
    return 0


if __name__ == "__main__":
    sys.exit(main())
