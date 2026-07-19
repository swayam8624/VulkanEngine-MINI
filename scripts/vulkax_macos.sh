#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${VULKAX_BUILD_DIR:-$ROOT/build}"
JOBS="${VULKAX_JOBS:-$(sysctl -n hw.logicalcpu 2>/dev/null || echo 8)}"

usage() {
  cat <<EOF
Usage: scripts/vulkax_macos.sh <command> [application arguments]

Commands:
  doctor  Check the local compiler, Vulkan, build, and Apple container tools
  deps    Install Homebrew build dependencies
  build   Configure and build a Release tree
  test    Build and run all CTest suites
  app     Build and open Connaught Place in the native macOS application
  london  Build and open Central London in the native macOS application
  tokyo   Build and open Central Tokyo in the native macOS application
  atlas   Build and run the experimental globe research view
  geo     Build and run the GeoBEACON city renderer

Environment:
  VULKAX_BUILD_DIR  Build directory, default: ./build
  VULKAX_JOBS       Parallel build jobs
EOF
}

doctor() {
  local failed=0
  echo "Vulkax Atlas macOS environment"
  echo "  macOS:    $(sw_vers -productVersion)"
  echo "  CPU:      $(uname -m)"
  for tool in brew cmake ninja glslangValidator vulkaninfo python3 container; do
    if command -v "$tool" >/dev/null 2>&1; then
      printf '  %-10s %s\n' "$tool:" "$(command -v "$tool")"
    else
      printf '  %-10s missing\n' "$tool:"
      failed=1
    fi
  done

  if command -v vulkaninfo >/dev/null 2>&1; then
    vulkaninfo --summary 2>/dev/null |
      awk '/Vulkan Instance Version:/{print "  Vulkan:   "$4} /deviceName/{sub(/^.*= /,""); print "  GPU:      "$0; exit}'
  fi

  if command -v container >/dev/null 2>&1; then
    if container system status >/dev/null 2>&1; then
      echo "  container: running"
    else
      echo "  container: installed, service stopped"
    fi
  fi
  if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    local generator
    generator="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")"
    echo "  build:     configured with ${generator:-unknown generator}"
  else
    echo "  build:     not configured"
  fi
  return "$failed"
}

deps() {
  command -v brew >/dev/null 2>&1 || {
    echo "error: Homebrew is required: https://brew.sh" >&2
    exit 1
  }
  brew install cmake ninja glfw glm nlohmann-json sqlite curl vulkan-loader glslang
}

build() {
  local configure_args=(
    -S "$ROOT"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_TESTING=ON
  )
  if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    local generator
    generator="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")"
    echo "Reusing existing CMake generator: ${generator:-unknown}"
  else
    configure_args+=(-G Ninja)
    echo "Configuring a new Ninja build tree."
  fi
  cmake "${configure_args[@]}"
  cmake --build "$BUILD_DIR" --parallel "$JOBS"
}

command="${1:-}"
shift || true
case "$command" in
  doctor) doctor ;;
  deps) deps ;;
  build) build ;;
  test)
    build
    ctest --test-dir "$BUILD_DIR" --output-on-failure
    ;;
  app)
    build
    open -n "$BUILD_DIR/Vulkax.app" --args \
      --geo \
      --geo-policy geo-beacon-bounded \
      --geo-manifest "$ROOT/data/connaught_place/generated/geobeacon.json" \
      --geo-navigation "$ROOT/data/connaught_place/navigation.json" \
      --geo-cache-mode warm \
      --lights 500 \
      --width 1440 \
      --height 900 \
      "$@"
    ;;
  london)
    build
    open -n "$BUILD_DIR/Vulkax.app" --args \
      --geo \
      --geo-policy geo-beacon-bounded \
      --geo-manifest "$ROOT/data/central_london/generated/geobeacon.json" \
      --geo-navigation "$ROOT/data/central_london/navigation.json" \
      --geo-cache-mode warm \
      --lights 500 \
      --width 1440 \
      --height 900 \
      "$@"
    ;;
  tokyo)
    build
    open -n "$BUILD_DIR/Vulkax.app" --args \
      --geo \
      --geo-policy geo-beacon-bounded \
      --geo-manifest "$ROOT/data/central_tokyo/generated/geobeacon.json" \
      --geo-navigation "$ROOT/data/central_tokyo/navigation.json" \
      --geo-cache-mode warm \
      --lights 500 \
      --width 1440 \
      --height 900 \
      "$@"
    ;;
  atlas)
    build
    exec "$BUILD_DIR/VulkaxAtlas" \
      --atlas-manifest "$ROOT/data/atlas/regions/delhi-ncr/atlas-dataset.json" \
      --atlas-navigation-replay "$ROOT/data/atlas/navigation_replay.json" \
      "$@"
    ;;
  geo)
    build
    exec "$BUILD_DIR/LveEngine" --geo "$@"
    ;;
  *) usage; exit 2 ;;
esac
