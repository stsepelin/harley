#!/usr/bin/env python3
"""
region.py - one command: OpenStreetMap .osm.pbf -> cluster .zmta archive.

Wraps the manual osmium + bake + pack pipeline (see README) so a real region
becomes an SD-ready map in a single step: clip -> keep roads+water -> export ->
bake per-tile ZMT0 -> pack ZMTA. Copy the result to the card as /sdcard/map.zmta
(CONFIG_VROD_MAP_SD_PATH) for a real (non-demo) map build.

Examples:
  # A local Geofabrik extract, clipped to a bbox:
  python3 region.py --pbf ~/Downloads/estonia-260707.osm.pbf \
      --bbox 24.60,59.36,24.95,59.50 --out tallinn.zmta

  # Center + radius instead of a bbox (km):
  python3 region.py --pbf estonia.osm.pbf --center 59.437,24.745 --radius 8 \
      --out tallinn.zmta

  # Fetch the extract from Geofabrik first (cached under downloads/):
  python3 region.py --url https://download.geofabrik.de/europe/estonia-latest.osm.pbf \
      --center 59.437,24.745 --radius 8 --out tallinn.zmta

Needs `osmium` (brew install osmium-tool). bake.py/pack.py are stdlib-only.
"""
import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))


def die(msg):
    sys.exit(f"region.py: {msg}")


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd))
    subprocess.run(cmd, check=True, **kw)


def download(url, dest):
    if os.path.exists(dest):
        print(f"cached {dest} ({os.path.getsize(dest) / 1e6:.0f} MB)")
        return
    print(f"downloading {url}")
    tmp = dest + ".part"
    with urllib.request.urlopen(url) as r, open(tmp, "wb") as f:
        total = int(r.headers.get("Content-Length", 0))
        got = 0
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            got += len(chunk)
            if total:
                print(f"\r  {got / 1e6:6.0f} / {total / 1e6:.0f} MB", end="", flush=True)
    print()
    os.replace(tmp, dest)


def bbox_from_center(lat, lon, radius_km):
    dlat = radius_km / 111.0
    dlon = radius_km / (111.0 * math.cos(math.radians(lat)))
    return (lon - dlon, lat - dlat, lon + dlon, lat + dlat)


def main():
    ap = argparse.ArgumentParser(
        description="OSM .osm.pbf -> cluster .zmta in one step.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--pbf", help="local .osm.pbf extract")
    src.add_argument("--url", help="Geofabrik .osm.pbf URL (cached under downloads/)")
    area = ap.add_mutually_exclusive_group()
    area.add_argument("--bbox", help="minlon,minlat,maxlon,maxlat")
    area.add_argument("--center", help="lat,lon (needs --radius)")
    ap.add_argument("--radius", type=float, help="km, used with --center")
    ap.add_argument("--zoom", type=int, default=16)
    ap.add_argument("--out", required=True, help="output .zmta path")
    ap.add_argument("--keep", action="store_true", help="keep the temp work dir")
    ap.add_argument("--python", default=sys.executable, help="python for bake/pack")
    args = ap.parse_args()

    if not shutil.which("osmium"):
        die("osmium not found - `brew install osmium-tool`")

    # 1. resolve the source .pbf.
    if args.url:
        cache = os.path.join(HERE, "downloads")
        os.makedirs(cache, exist_ok=True)
        pbf = os.path.join(cache, os.path.basename(args.url))
        download(args.url, pbf)
    else:
        pbf = os.path.abspath(os.path.expanduser(args.pbf))
        if not os.path.exists(pbf):
            die(f"no such file: {pbf}")

    # 2. resolve the clip bbox.
    bbox = None
    if args.bbox:
        bbox = tuple(float(x) for x in args.bbox.split(","))
        if len(bbox) != 4:
            die("--bbox wants minlon,minlat,maxlon,maxlat")
    elif args.center:
        lat, lon = (float(x) for x in args.center.split(","))
        if not args.radius:
            die("--center needs --radius")
        bbox = bbox_from_center(lat, lon, args.radius)

    work = tempfile.mkdtemp(prefix="zmta_")
    try:
        cur = pbf
        if bbox:
            clipped = os.path.join(work, "area.pbf")
            run(["osmium", "extract", "-b", ",".join(f"{v:.6f}" for v in bbox),
                 cur, "-o", clipped, "--overwrite"])
            cur = clipped
        else:
            print("no --bbox/--center: baking the WHOLE extract (large + slow)")

        rw = os.path.join(work, "rw.pbf")
        run(["osmium", "tags-filter", cur, "w/highway", "w/waterway",
             "n/natural=water", "a/natural=water", "a/water", "a/building",
             "-o", rw, "--overwrite"])

        geo = os.path.join(work, "area.geojsonl")
        run(["osmium", "export", rw, "--geometry-types=linestring,polygon",
             "-f", "geojsonseq", "-o", geo, "--overwrite"])

        tiles = os.path.join(work, "tiles")
        run([args.python, os.path.join(HERE, "bake.py"), geo, tiles, "--zoom", str(args.zoom)],
            cwd=HERE)  # cwd=HERE so bake.py can import styles.py

        out = os.path.abspath(os.path.expanduser(args.out))
        run([args.python, os.path.join(HERE, "pack.py"), tiles, out], cwd=HERE)

        print(f"\nDONE  {out}  ({os.path.getsize(out) / 1024:.1f} KB)")
        print("Copy to the SD card as /sdcard/map.zmta (or match CONFIG_VROD_MAP_SD_PATH),")
        print("or preview it in the sim: VROD_MAP_ZMTA=%s ./build/vrod_sim" % out)
    finally:
        if args.keep:
            print(f"kept work dir: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
