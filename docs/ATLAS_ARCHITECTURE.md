# Vulkax Atlas Architecture

## Coordinate model

Persistent positions use WGS84 latitude, longitude, and ellipsoidal height. Planetary calculations
use double-precision ECEF. Regional data uses an East-North-Up local frame. Render vertices are
converted to camera-relative floats, keeping large ECEF magnitudes out of shader arithmetic.

The globe is addressed through six cube faces and a quadtree per face. `AtlasTileKey` combines
face, level, X, Y, and semantic layer. `GlobeTileSelector` rejects tiles below the ellipsoid
horizon or outside the camera view, refines by screen-space error, applies route-corridor bias, and
enforces a hard selected-tile bound.

## Runtime pipeline

1. `AtlasDatasetManifest` loads dataset version 2 and layer templates.
2. `GlobeTileSelector` produces visible candidates.
3. `RoutePredictiveScheduler` ranks candidates by visibility, SSE, semantics, route probability,
   deadline, upload cost, memory cost, and measured frame pressure.
4. `AtlasRuntime` checks the disk cache, issues cancellable asynchronous requests, applies retry
   backoff, and exposes completed payloads.
5. A `TileSource` reads from files, memory, HTTP, or a SQLite `.vxa` archive.
6. The renderer consumes ready GLB payloads through device-local upload and draw infrastructure.

## Navigation boundary

`SearchProvider`, `RouteProvider`, `TransitProvider`, and `TrafficProvider` isolate Atlas from
backend-specific schemas. `GatewayNavigationProvider` uses the normalized `/v1` JSON contract.
`ReplayNavigationProvider` provides deterministic tests and demonstrations. `.vxa` packs provide
offline POI search and archive routing assets alongside render tiles.

## Platform boundary

`VulkanSurfaceHost` owns the platform-specific surface, extent, resize state, instance extensions,
and event wait. Vulkan device and swapchain code depend only on that interface. GLFW is the current
desktop host; Qt, Android `ANativeWindow`, and headless hosts can implement the same boundary.

## Research separation

BEACON metrics preserve predicted diffuse error separately from rendered MSE, PSNR, SSIM, and
maximum error. Atlas benchmark rows add selected/visited/cull/depth/route-bias metrics while
retaining GPU query-pool versus CPU-fallback timing classification.
