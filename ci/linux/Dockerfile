# Linux build and tests of the port (re/crossplatform.md, "Как проверять под Linux").
#   docker build -t tsport-linux ci/linux
FROM debian:trixie
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential clang cmake ninja-build pkg-config ca-certificates \
    qt6-base-dev qt6-base-private-dev qt6-base-dev-tools qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools \
    libgl-dev libxkbcommon-dev libxkbcommon-x11-dev libxkbregistry-dev libxcb1-dev libxcb-xkb-dev xkb-data libx11-data \
    fonts-liberation fonts-dejavu-core xvfb xauth \
    && rm -rf /var/lib/apt/lists/*
