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

## Run the globe

```bash
scripts/vulkax_macos.sh atlas
```

Controls:

- `W`, `A`, `S`, `D`: move horizontally
- `E`, `Q`: move vertically
- Arrow keys: rotate the view
- Either Shift key: accelerate movement
- Escape or close window: exit

Additional Atlas command-line options can be appended to the launcher:

```bash
scripts/vulkax_macos.sh atlas --width 1920 --height 1080
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
