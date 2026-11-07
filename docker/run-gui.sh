#!/usr/bin/env bash
# Arranca bridge + GUI com o cookie X11 correto (sem "xhost +").
set -euo pipefail

: "${DISPLAY:?DISPLAY não definido — corre isto dentro de uma sessão gráfica}"

XAUTH="${XAUTHORITY_FILE:-/tmp/.docker.xauth}"

if [ ! -S /tmp/.X11-unix/X"${DISPLAY#*:}" ]; then
    echo "AVISO: socket X11 de ${DISPLAY} não encontrado em /tmp/.X11-unix."
    echo "       Em Wayland puro, garante que o XWayland está a correr."
fi

touch "$XAUTH"
xauth nlist "$DISPLAY" | sed -e 's/^..../ffff/' | xauth -f "$XAUTH" nmerge -
chmod 644 "$XAUTH"

export DOCKER_UID="$(id -u)"
export DOCKER_GID="$(id -g)"
export XAUTHORITY_FILE="$XAUTH"

exec docker compose -f "$(dirname "$0")/docker-compose.yml" up --build "$@"
