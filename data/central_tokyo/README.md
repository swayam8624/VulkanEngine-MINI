# Central Tokyo GeoBEACON Dataset

This is the third complete Vulkax city slice. It covers Tokyo Station, Marunouchi, Ginza, and the
eastern edge of the Imperial Palace:

```text
south = 35.6740
west  = 139.7520
north = 35.6865
east  = 139.7750
```

The checked `source.osm` extract is OpenStreetMap data licensed under ODbL 1.0.

Attribution: © OpenStreetMap contributors  
License and attribution details: https://www.openstreetmap.org/copyright

Regenerate the extract, semantic LOD tiles, and local navigation graph explicitly:

```bash
python3 tools/fetch_osm_extract.py \
  --bbox 35.6740 139.7520 35.6865 139.7750 \
  --output data/central_tokyo/source.osm \
  --force

python3 tools/build_geobeacon_tiles.py \
  --source data/central_tokyo/source.osm \
  --output data/central_tokyo/generated \
  --bbox 35.6740 139.7520 35.6865 139.7750 \
  --dataset-id central-tokyo \
  --display-name "Central Tokyo"

python3 tools/build_connaught_navigation.py \
  --source data/central_tokyo/source.osm \
  --output data/central_tokyo/navigation.json \
  --region central-tokyo \
  --display-name "Central Tokyo" \
  --subtitle "Central Tokyo, Japan"
```

The renderer never contacts OpenStreetMap or Overpass during build or runtime.
