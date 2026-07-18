# Connaught Place GeoBEACON Dataset

The source extract is OpenStreetMap data licensed under ODbL 1.0.

Attribution: © OpenStreetMap contributors  
License and attribution details: https://www.openstreetmap.org/copyright

Refresh the canonical extract explicitly:

```bash
python3 tools/fetch_osm_extract.py --force
python3 tools/build_geobeacon_tiles.py
```

The renderer never contacts OpenStreetMap or Overpass during build or runtime.
