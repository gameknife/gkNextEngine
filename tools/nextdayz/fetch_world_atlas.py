"""Refresh the offline vector atlas. Natural Earth 1:110m land is public domain."""
import json
from pathlib import Path
from urllib.request import urlopen

SOURCE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_land.geojson"

def main():
    with urlopen(SOURCE, timeout=60) as response:
        geojson = json.load(response)
    rings = []
    for feature in geojson["features"]:
        geometry = feature["geometry"]
        polygons = geometry["coordinates"] if geometry["type"] == "MultiPolygon" else [geometry["coordinates"]]
        for polygon in polygons:
            # At atlas scale the coastline alone is sufficient; inland holes are omitted.
            ring = [[round(x, 3), round(y, 3)] for x, y, *_ in polygon[0]]
            if ring[0] == ring[-1]:
                ring.pop()
            if len(ring) >= 3:
                rings.append(ring)
    target = Path(__file__).resolve().parents[2] / "assets/configs/nextdayz/world_land.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps({"format": "nextdayz-atlas1", "source": SOURCE,
        "attribution": "Made with Natural Earth. Public domain. https://www.naturalearthdata.com/about/terms-of-use/",
        "polygons": rings}, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"Vector atlas: {len(rings)} land polygons, {sum(map(len, rings))} vertices -> {target}")

if __name__ == "__main__":
    main()
