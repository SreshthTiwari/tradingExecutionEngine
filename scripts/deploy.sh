#!/usr/bin/env bash
# Build and run the engine in a local Docker container.
#
#   scripts/deploy.sh build            build the image (compiles and runs the tests)
#   scripts/deploy.sh run [port]       (re)start the container, published on 127.0.0.1:<port>
#   scripts/deploy.sh stop             stop and remove the container
#   scripts/deploy.sh logs             follow the container logs
#   scripts/deploy.sh                  build, then run on the default port
#
# Overrides: IMAGE (default trading-engine), CONTAINER (default trading-engine), PORT (default 9000)
set -euo pipefail

IMAGE="${IMAGE:-trading-engine}"
CONTAINER="${CONTAINER:-trading-engine}"
PORT="${PORT:-9000}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

command -v docker >/dev/null 2>&1 || { echo "error: docker is not installed or not on PATH" >&2; exit 1; }

build() {
    docker build -t "$IMAGE" "$ROOT"
}

stop() {
    docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
}

run() {
    local port="${1:-$PORT}"
    stop
    # published on loopback only: the gateway has no authentication
    docker run -d --name "$CONTAINER" -p "127.0.0.1:${port}:9000" --restart unless-stopped "$IMAGE" >/dev/null
    echo "running: $CONTAINER on 127.0.0.1:${port}"
    echo "try:     printf 'NEW 1 S 5000 10\\n' | nc 127.0.0.1 ${port}"
}

case "${1:-all}" in
    build) build ;;
    run)   run "${2:-}" ;;
    stop)  stop ;;
    logs)  docker logs -f "$CONTAINER" ;;
    all)   build; run ;;
    *)     echo "usage: $0 [build | run [port] | stop | logs]" >&2; exit 2 ;;
esac
