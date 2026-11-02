"""Build the bundled map from Natural Earth 5.1.2 country geometry.
Input is downloaded explicitly by the developer, never by the application.
Usage: python scripts/build-map.py path/to/ne_50m_admin_0_countries.geojson
"""
import hashlib
import json
import math
import pathlib
import sys

SOURCE_SHA256 = "3e458fc036ad0a66411f2c1e6cac49c5d7bfb81cb1123bc513b22511a2b7fdeb"


def simplify(points, epsilon=0.16):
    if len(points) < 3:
        return points
    a, b = points[0], points[-1]
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = dx * dx + dy * dy
    def distance(p):
        t = max(0, min(1, ((p[0]-a[0])*dx+(p[1]-a[1])*dy)/length)) if length else 0
        return math.hypot(p[0]-a[0]-t*dx, p[1]-a[1]-t*dy)
    index, largest = max(((i, distance(p)) for i, p in enumerate(points[1:-1], 1)), key=lambda p: p[1])
    if largest > epsilon:
        return simplify(points[:index+1], epsilon)[:-1] + simplify(points[index:], epsilon)
    return [a, b]


def build(source):
    raw = pathlib.Path(source).read_bytes()
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise ValueError("Unexpected Natural Earth source; review provenance before changing the pin")
    countries = []
    for feature in json.loads(raw)["features"]:
        p, geometry = feature["properties"], feature["geometry"]
        if p["ADM0_A3"] == "ATA":
            continue
        code = p["ISO_A2_EH"]
        if code == "-99":
            code = "XK" if p["ADM0_A3"] == "KOS" else ""
        polygons = geometry["coordinates"] if geometry["type"] == "MultiPolygon" else [geometry["coordinates"]]
        rings = []
        for polygon in polygons:
            # Exterior outlines only; this is an illustrative basemap, not a boundary dataset.
            ring = polygon[0]
            points = simplify(ring)
            if len(points) < 4:
                continue
            rings.append([[round(x, 3), round(y, 3)] for x, y in points])
        countries.append(dict(code=code, name=p["NAME_EN"], longitude=p["LABEL_X"],
                              latitude=p["LABEL_Y"], rings=rings))
    result = dict(version=1, source="Natural Earth 5.1.2; public domain",
                  projection="equirectangular", countries=countries)
    target = pathlib.Path(__file__).resolve().parent.parent / "data" / "world-map.json"
    target.write_text(json.dumps(result, ensure_ascii=False, separators=(",", ":"))+"\n", encoding="utf-8")
    print(json.dumps(dict(countries=len(countries), vertices=sum(len(r) for c in countries for r in c["rings"]),
                          bytes=target.stat().st_size, sha256=hashlib.sha256(target.read_bytes()).hexdigest())))


if __name__ == "__main__":
    build(sys.argv[1])
