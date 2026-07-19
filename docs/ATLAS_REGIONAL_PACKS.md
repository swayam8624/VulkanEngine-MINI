# Vulkax Atlas Regional Packs

`.vxa` is a transactional SQLite archive for offline Atlas regions. It stores:

- render tiles keyed by cube face, level, X, Y, and layer;
- ETags and checksums;
- offline POIs and ranked local search;
- named routing, transit, and supporting assets;
- dataset, attribution, and source metadata.

Pack writes use a transaction and checkpoint before switching back from WAL mode. Runtime reads use
independent read-only SQLite connections, so worker requests do not share a mutable connection.
Pack lookup is independent of the original content URI.

Create the checked compatibility pack:

```bash
build/atlas-build pack-geobeacon \
  data/connaught_place/generated/geobeacon.json \
  build/connaught-place.vxa
sqlite3 build/connaught-place.vxa \
  'pragma integrity_check; select level,count(*) from tiles group by level;'
```

The five source manifests under `config/atlas/regions` define Delhi NCR, Greater London, Tokyo
metro, New York metro, and the Swiss Alps. Generated render packs are intentionally outside Git;
the checked dataset manifests and six-root 3D Tiles descriptors are byte-for-byte reproducible.

Every pack must retain per-source license, timestamp, checksum, vertical datum, and permanent
OpenStreetMap attribution. API credentials are never stored in packs or logs.
