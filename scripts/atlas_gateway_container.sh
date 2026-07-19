#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${ATLAS_GATEWAY_IMAGE:-vulkax-atlas-gateway:local}"
NAME="${ATLAS_GATEWAY_CONTAINER:-vulkax-atlas-gateway}"
PORT="${ATLAS_GATEWAY_PORT:-8080}"

usage() {
  cat <<EOF
Usage: scripts/atlas_gateway_container.sh <command>

Commands:
  setup    Start Apple container services and install the recommended kernel
  build    Build the Vulkax Atlas gateway OCI image
  start    Build and start the gateway in the background
  stop     Stop and remove the gateway container
  restart  Stop and start the gateway
  status   Show Apple container and gateway status
  logs     Follow gateway logs
  health   Check /v1/status

Optional environment:
  ATLAS_GATEWAY_PORT       Host port, default: 8080
  ATLAS_GATEWAY_IMAGE      OCI image name
  ATLAS_GATEWAY_CONTAINER  Container name
  PELIAS_URL               Self-hosted Pelias base URL
  VALHALLA_URL             Self-hosted Valhalla base URL
EOF
}

require_container() {
  if ! command -v container >/dev/null 2>&1; then
    echo "error: Apple container is not installed." >&2
    echo "Install it from https://github.com/apple/container/releases" >&2
    exit 1
  fi
}

container_exists() {
  container list --all --format json 2>/dev/null |
    python3 -c 'import json,sys
name=sys.argv[1]
data=json.load(sys.stdin)
items=data if isinstance(data,list) else data.get("containers",[])
raise SystemExit(0 if any(str(x.get("id","")) == name or x.get("name") == name for x in items) else 1)' "$NAME"
}

setup() {
  require_container
  container system start --enable-kernel-install
  container system status
}

build() {
  require_container
  container build \
    --file "$ROOT/services/atlas_gateway/Containerfile" \
    --progress plain \
    --tag "$IMAGE" \
    "$ROOT"
}

stop() {
  require_container
  if container_exists; then
    container stop "$NAME" || true
    container delete "$NAME" || true
  fi
}

start() {
  require_container
  if ! container system status >/dev/null 2>&1; then
    echo "Apple container services are not running; starting them."
    setup
  fi

  build
  stop

  args=(
    run --detach --progress plain --name "$NAME"
    --cpus 2 --memory 512M
    --publish "127.0.0.1:${PORT}:8080"
    --mount "type=bind,source=${ROOT}/data,target=/srv/vulkax/content,readonly"
    --env "ATLAS_CONTENT_ROOT=/srv/vulkax/content"
  )
  if [[ -n "${PELIAS_URL:-}" ]]; then
    args+=(--env "PELIAS_URL=${PELIAS_URL}")
  fi
  if [[ -n "${VALHALLA_URL:-}" ]]; then
    args+=(--env "VALHALLA_URL=${VALHALLA_URL}")
  fi
  args+=("$IMAGE")

  container "${args[@]}"
  echo "Gateway started at http://127.0.0.1:${PORT}"

  for _ in {1..30}; do
    if curl --fail --silent "http://127.0.0.1:${PORT}/v1/status" >/dev/null; then
      health
      return
    fi
    sleep 0.5
  done

  echo "error: gateway did not become healthy" >&2
  container logs "$NAME" >&2 || true
  exit 1
}

health() {
  curl --fail --silent --show-error \
    "http://127.0.0.1:${PORT}/v1/status"
  printf '\n'
}

command="${1:-}"
case "$command" in
  setup) setup ;;
  build) build ;;
  start) start ;;
  stop) stop ;;
  restart) stop; start ;;
  status)
    require_container
    container system status
    container list --all
    ;;
  logs)
    require_container
    container logs --follow "$NAME"
    ;;
  health) health ;;
  *) usage; exit 2 ;;
esac
