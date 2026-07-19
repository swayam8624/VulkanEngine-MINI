# Vulkax Atlas

Vulkax Atlas is a C++20 Vulkan globe and navigation SDK built on the reproducible BEACON and
GeoBEACON research renderer. The current Atlas runtime provides WGS84 geodesy, double-precision
ECEF and local frames, six-face cube-quadtree addressing, horizon/SSE tile selection,
route-predictive resource scheduling, asynchronous file/HTTP/memory/SQLite tile sources, disk
cache control, `.vxa` regional packs, normalized navigation APIs, deterministic replay, and a
rendered WGS84 globe with route geometry.

`VulkaxAtlas` is the consumer globe entrypoint. `LveEngine` remains the BEACON/GeoBEACON research
and regression executable, preserving every existing technique and CLI identifier.

## Atlas quick start

```bash
scripts/vulkax_macos.sh doctor
scripts/vulkax_macos.sh deps
scripts/vulkax_macos.sh test
scripts/vulkax_macos.sh atlas
```

The globe uses `W/A/S/D` for horizontal movement, `E/Q` for vertical movement, arrow keys for
view rotation, and either Shift key for accelerated traversal. The window reports selected globe
tiles and deepest selected quadtree level while permanently retaining OpenStreetMap attribution.
The dependency step only needs to be run once. Later sessions normally need only
`scripts/vulkax_macos.sh atlas`.

Generate and validate the five checked reference manifests:

```bash
for region in delhi-ncr greater-london tokyo-metro new-york-metro swiss-alps; do
  build/atlas-build generate-manifest \
    config/atlas/regions/$region.json \
    data/atlas/regions/$region/atlas-dataset.json
  build/atlas-build validate data/atlas/regions/$region/atlas-dataset.json
done
```

Create an offline `.vxa` pack from the checked Connaught Place GeoBEACON database:

```bash
build/atlas-build pack-geobeacon \
  data/connaught_place/generated/geobeacon.json \
  build/connaught-place.vxa
```

## Navigation gateway

The SDK and applications use one self-hostable contract: `/v1/search`, `/v1/reverse`,
`/v1/route`, `/v1/transit`, `/v1/traffic`, `/v1/status`, and range-addressable
`/v1/content/*`. Run the deterministic local gateway:

```bash
python3 services/atlas_gateway/server.py \
  --replay data/atlas/navigation_replay.json \
  --content-root data
```

Connect self-hosted Pelias and Valhalla without changing clients:

```bash
PELIAS_URL=http://127.0.0.1:4000 \
VALHALLA_URL=http://127.0.0.1:8002 \
python3 services/atlas_gateway/server.py \
  --replay data/atlas/navigation_replay.json
```

Pelias autocomplete/reverse GeoJSON and Valhalla route/polyline6 responses are normalized into the
Atlas provider model. If an explicitly configured upstream fails, the gateway returns `502` rather
than silently substituting replay data. Transit and traffic replay remain deterministic unless
their deployment-specific adapters are configured separately.

On Apple silicon with macOS 26 or newer, run the gateway with Apple `container`:

```bash
scripts/atlas_gateway_container.sh setup
scripts/atlas_gateway_container.sh start
scripts/atlas_gateway_container.sh health
```

The first `setup` may request permission to install Apple's recommended Linux kernel. Later
sessions only need `start`; use `stop`, `restart`, `status`, or `logs` for lifecycle management.
The recipe is the OCI-standard `services/atlas_gateway/Containerfile`, and the runtime binds the
checked `data` directory read-only for range-addressable content.

To connect real self-hosted services:

```bash
PELIAS_URL=http://pelias-host:4000 \
VALHALLA_URL=http://valhalla-host:8002 \
scripts/atlas_gateway_container.sh restart
```

The native `GatewayNavigationProvider` and `CurlHttpTransport` consume this contract. The same
provider interfaces also support deterministic replay and offline SQLite POI search.

## Atlas research claim

Atlas evaluates route-predictive joint allocation of tile detail, residency, upload bandwidth,
cluster depth, light-list representation, and bounded diffuse-light error. Globe rendering,
routing, LOD, HTTP streaming, and clustered lighting are supporting systems; the research variable
is whether route probability, time-to-arrival, maneuver importance, and semantic importance reduce
deadline misses and wasted prefetch bandwidth at a fixed frame/memory budget.

Architecture and dataset details are in [docs/ATLAS_ARCHITECTURE.md](docs/ATLAS_ARCHITECTURE.md)
and [docs/ATLAS_REGIONAL_PACKS.md](docs/ATLAS_REGIONAL_PACKS.md). The complete local operating
guide is [docs/RUNNING_VULKAX.md](docs/RUNNING_VULKAX.md).

## Checked Atlas controller result

`docs/results/atlas_scheduler_current` contains a 600-frame constrained scheduler experiment across
seven policies. It is explicitly an analytical scheduler simulation, not a Vulkan timing set.

| Policy | Route-corridor deadline misses | Wasted prefetch bytes |
| --- | ---: | ---: |
| distance-only | 644 | 3,145,728 |
| velocity-only | 204 | 2,818,048 |
| route-only | 8 | 2,686,976 |
| route-semantics | 4 | 2,686,976 |
| full-atlas | 4 | 2,686,976 |

Regenerate the raw rows and figures:

```bash
build/atlas-benchmark --frames 600 \
  --output docs/results/atlas_scheduler_current
python3 scripts/generate_atlas_figures.py \
  docs/results/atlas_scheduler_current \
  docs/results/atlas_scheduler_current/figures
```

The dedicated paper is
[`docs/paper/vulkax_atlas_research.pdf`](docs/paper/vulkax_atlas_research.pdf).

## GeoBEACON foundation

GeoBEACON is the checked urban digital-twin research baseline built around an OpenStreetMap extract
of Connaught Place, New Delhi. It provides semantic tile selection, asynchronous city streaming,
memory and upload budgets, deterministic camera routes, exact diffuse reference captures, and
separate GPU-query, CPU-fallback, and analytical measurement classes.

## Research claim

GeoBEACON evaluates whether joint semantic and view-aware allocation of geometry detail, streaming
bandwidth, resident memory, and diffuse-light accuracy provides more task-relevant utility per
millisecond than fixed LOD, distance-only LOD, semantic-only LOD, or lighting-only adaptation.
SSBO lighting, instancing, clustered lighting, bounded pruning, glTF, and 3D Tiles are supporting
infrastructure rather than novelty claims.

## Implemented policies

| CLI policy | Geometry allocation | Lighting |
| --- | --- | --- |
| `fixed-lod1` | LOD1 for every selected tile | camera-space fixed GPU clusters |
| `distance-lod` | distance thresholds | camera-space fixed GPU clusters |
| `semantic-lod` | distance scaled by semantic importance | camera-space fixed GPU clusters |
| `geo-beacon-exact` | semantic budget controller | adaptive exact diffuse lists |
| `geo-beacon-bounded` | semantic budget controller | adaptive aggregate-bounded pruning |

The original BEACON CLI identifiers remain available: `baseline`, `ssbo`, `ssbo-diffuse`,
`instanced`, `cpu-clustered`, `fixed-cluster-cost-model`, `gpu-clustered`, `adaptive-exact`,
`adaptive-bounded`, and `beacon`.

The fixed GPU clustered path projects finite-radius lights into screen tiles and logarithmic
camera-space depth slices. Assignment is implemented as clear, light-centric count, hierarchical
exclusive scan, offset propagation, and scatter compute stages. Overflow is flagged and rendered
through an exact all-light fallback.

## Dataset

The canonical study area is:

```text
Connaught Place, New Delhi
south 28.6270, west 77.2070, north 28.6365, east 77.2245
```

`data/connaught_place/source.osm` is the authoritative checked source. The deterministic
preprocessor emits 151 spatial tiles, three GLB representations per tile, a 3D Tiles 1.1
`tileset.json`, runtime metadata, checksums, and a validation report. Runtime rendering permanently
shows `© OpenStreetMap contributors`.

Regenerate the committed tile database:

```bash
python3 tools/build_geobeacon_tiles.py \
  --source data/connaught_place/source.osm \
  --output data/connaught_place/generated
ctest --test-dir build --output-on-failure
```

Network acquisition is explicit and never runs during build or startup:

```bash
python3 tools/fetch_osm_extract.py --output data/connaught_place/source.osm
```

## Build and test

macOS prerequisites include CMake, GLFW, GLM, nlohmann-json, a Vulkan loader, and
`glslangValidator`.

```bash
brew install cmake glfw glm nlohmann-json vulkan-loader glslang
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

List devices and select one deterministically:

```bash
build/LveEngine --list-devices
build/LveEngine --geo --device-index 0
build/LveEngine --geo --device-name "Apple M2 Pro"
build/LveEngine --geo --device-uuid 0000106b-1a05-0208-0000-000000000000
```

## Run

Interactive city rendering:

```bash
build/LveEngine --geo --geo-policy geo-beacon-bounded \
  --lights 500 --geo-budget-frame-ms 16.67 \
  --geo-budget-memory-mib 512 --geo-budget-upload-mibps 100
```

Interactive controls use `W/A/S/D` for horizontal movement, `E/Q` for vertical movement, arrow
keys for view rotation, and either Shift key for fast traversal. City mode uses metre-scale movement
speeds appropriate for the full Connaught Place extent.

Deterministic measured run:

```bash
build/LveEngine --geo --geo-policy geo-beacon-bounded \
  --geo-camera-path landmark-approach --geo-cache-mode cold \
  --lights 500 --width 1920 --height 1080 \
  --warmup-frames 600 --frames 1800 --capture-reference true \
  --output docs/results/my_run
```

Camera routes are `outer-orbit`, `street-drive`, `intersection-dwell`, `landmark-approach`, and
`rapid-teleport`. Each measured run writes frame-level CSV, a reproducibility manifest, exact and
test PPM captures when enabled, and a summary. Terminal runs include a progress bar unless
`--quiet` is supplied.

## Experiments

The full resumable matrix encodes five policies, five routes, 100/500/2,000 lights, two frame
budgets, three memory budgets, three upload limits, three resolutions, cold/warm caches, two local
ICDs, and five trials:

```bash
python3 scripts/run_geobeacon_matrix.py \
  --binary build/LveEngine \
  --output docs/results/geobeacon_full \
  --profile full
python3 scripts/generate_beacon_figures.py \
  docs/results/geobeacon_full \
  docs/results/geobeacon_full/figures
```

`smoke` and `core` profiles use the same runner for validation and intermediate studies.

## Checked results

`docs/results/geobeacon_smoke` contains 20 completed dual-driver pilot cases: all five policies,
outer-orbit and rapid-teleport, 100 lights, a 256 MiB memory budget, a 25 MiB/s upload limit, 30
warm-up frames, and 60 measured frames. This is an execution and instrumentation validation set,
not the full repeated-trial claim.

| Driver / timing class | Policy | CPU p50 ms | CPU p95 ms | GPU cluster + light p50 ms |
| --- | --- | ---: | ---: | ---: |
| MoltenVK / Vulkan timestamps | fixed LOD1 | 8.232 | 11.920 | 0.3086 |
| MoltenVK / Vulkan timestamps | GeoBEACON exact | 8.108 | 12.651 | 0.2866 |
| MoltenVK / Vulkan timestamps | GeoBEACON bounded | 7.915 | 12.361 | 0.2621 |
| KosmicKrisp / Vulkan CPU fallback | fixed LOD1 | 16.562 | 25.909 | unavailable |
| KosmicKrisp / Vulkan CPU fallback | GeoBEACON exact | 16.046 | 21.421 | unavailable |
| KosmicKrisp / Vulkan CPU fallback | GeoBEACON bounded | 16.105 | 21.838 | unavailable |

MoltenVK reports timestamp-query support on this Apple M2 Pro. KosmicKrisp does not, so its rows
are explicitly classified as Vulkan CPU measurements. Exact and bounded runs store rendered MSE,
PSNR, SSIM, maximum pixel error, and temporal MSE variation separately from conservative modeled
bounds. Every policy is compared with a separately streamed maximum-LOD diffuse reference; the
pilot's mean MSE ranges from `3.09e-4` for fixed LOD1 to approximately `5.11e-4` for the semantic
adaptive policies.

## Key files

- `tools/build_geobeacon_tiles.py`: deterministic OSM-to-GLB/3D Tiles preprocessing
- `src/geobeacon/geo_scene.cpp`: tile selection, worker loading, upload integration, and eviction
- `src/geobeacon/geo_camera_path.cpp`: deterministic benchmark routes
- `src/beacon/adaptive_vulkan_builder.cpp`: adaptive hierarchy, pruning, encoding, and hysteresis
- `src/systems/clustered_lighting_system.cpp`: count/scan/scatter GPU assignment
- `src/beacon/offscreen_comparison.cpp`: exact captures and image metrics
- `scripts/run_geobeacon_matrix.py`: resumable research matrix
- `scripts/generate_beacon_figures.py`: raw-CSV figure regeneration
- `scripts/android/`: Android Vulkan capability and benchmark harness
- `.github/workflows/ci.yml`: Linux build, CTest, and Lavapipe smoke validation
- `docs/paper/geobeacon_research.tex`: paper source
- `docs/paper/geobeacon_research.pdf`: compiled paper

## Research scope

The formal error account covers opaque, unshadowed diffuse finite-radius point lighting. Strict
specular bounds, shadows, transparency, neural rendering, imagery, Gaussian splats, sensor
simulation, ROS, CARLA, and live network streaming are outside the evaluated claim.

## Attribution and licenses

Engine and preprocessing code are MIT licensed. The renderer substrate derives from Brendan
Galea's Little Vulkan Engine tutorial under MIT. The checked OSM extract and derived tile database
are covered separately by ODbL; see `data/connaught_place/LICENSE-ODbL.md`.

Map data: © OpenStreetMap contributors,
[copyright and license](https://www.openstreetmap.org/copyright).
