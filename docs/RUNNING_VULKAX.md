# Running Vulkax Atlas on macOS

## One-time setup

Vulkax Atlas requires an Apple-silicon Mac, a working Vulkan loader/driver, CMake, Ninja, and the
shader compiler. Check the machine first:

```bash
scripts/vulkax_macos.sh doctor
```

Install or refresh the native dependencies:

```bash
scripts/vulkax_macos.sh deps
```

Apple `container` is only required for the normalized navigation gateway. Install the signed
package from the Apple container releases page, then initialize its service and Linux kernel:

```bash
scripts/atlas_gateway_container.sh setup
```

The kernel installation is the only step that may require an interactive confirmation or an
administrator password.

## Validate the checkout

```bash
scripts/vulkax_macos.sh test
```

This creates a Release Ninja build, compiles all shaders and executables, and runs the complete
CTest suite.

## Run Connaught Place

Open the packaged native macOS application:

```bash
scripts/vulkax_macos.sh app
```

The generated application is `build/Vulkax.app`. It opens the complete checked Connaught Place
GeoBEACON dataset. Its native map panel provides:

- offline place and road search;
- walking, driving, and cycling route calculation from the current camera position;
- a rendered route ribbon over the city;
- automatic route-follow camera movement;
- clear route and route status controls.

Search and routing use `data/connaught_place/navigation.json`. They do not require Apple
`container`, the Atlas gateway, Pelias, Valhalla, or an API key. For terminal-attached city
diagnostics and benchmark output:

```bash
scripts/vulkax_macos.sh geo --geo-policy geo-beacon-bounded
```

The incomplete globe experiment is deliberately separate:

```bash
scripts/vulkax_macos.sh atlas
```

Controls:

- Search field and **Search**: find checked OSM places and roads
- Result and travel-mode selectors: choose a destination and walking, driving, or cycling
- **Route**: calculate and draw a local route
- **Follow**: move the camera along the active route
- **Clear**: remove the active route
- `W`, `A`, `S`, `D`: move horizontally
- `E`, `Q`: move vertically
- Arrow keys: rotate the view
- Either Shift key: accelerate movement
- Escape or close window: exit

Additional Atlas command-line options can be appended to the launcher:

```bash
scripts/vulkax_macos.sh atlas --width 1920 --height 1080
```

Regenerate the checked navigation graph from the canonical OSM extract:

```bash
python3 tools/build_connaught_navigation.py \
  --source data/connaught_place/source.osm \
  --output data/connaught_place/navigation.json
ctest --test-dir build -R connaught_navigation --output-on-failure
```

## Run the navigation gateway

Start and verify the deterministic offline service:

```bash
scripts/atlas_gateway_container.sh start
scripts/atlas_gateway_container.sh health
```

The endpoint is `http://127.0.0.1:8080`. The image includes deterministic search, routing,
transit, and traffic replay. Checked content is mounted read-only from `data`.

Useful lifecycle commands:

```bash
scripts/atlas_gateway_container.sh status
scripts/atlas_gateway_container.sh logs
scripts/atlas_gateway_container.sh restart
scripts/atlas_gateway_container.sh stop
```

Pelias and Valhalla are optional. When available, provide their URLs while starting the gateway:

```bash
PELIAS_URL=http://pelias-host:4000 \
VALHALLA_URL=http://valhalla-host:8002 \
scripts/atlas_gateway_container.sh restart
```

Without those upstreams, replay mode remains fully deterministic and offline.

## Run GeoBEACON

```bash
scripts/vulkax_macos.sh geo \
  --geo-policy geo-beacon-bounded \
  --lights 500 \
  --geo-budget-frame-ms 16.67
```

## Troubleshooting

Re-run `scripts/vulkax_macos.sh doctor` first. If the gateway is unavailable, check
`scripts/atlas_gateway_container.sh status` and `logs`. If Vulkan device selection is ambiguous:

```bash
build/LveEngine --list-devices
build/LveEngine --geo --device-index 0
```

Delete `build` only when a clean reconfiguration is required; generated datasets and checked
research results do not need to be removed.
