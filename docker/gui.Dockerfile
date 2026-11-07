# syntax=docker/dockerfile:1.7
ARG DEBIAN=trixie

# ---------- build ----------
FROM debian:${DEBIAN} AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        pkg-config \
        libgtk-4-dev \
        libglib2.0-dev \
    && rm -rf /var/lib/apt/lists/*

# ✅ CRIAR a pasta de saída ANTES de compilar
RUN mkdir -p /out

WORKDIR /src

COPY gui/main.c ./
COPY gui/transport.c ./
COPY gui/transport.h ./

RUN cc -O2 -g -std=c11 -Wall -Wextra \
        -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=500 \
        -I/usr/include/x86_64-linux-gnu \
        -o /out/fydelisbot-gui main.c transport.c \
        $(pkg-config --cflags --libs gtk4 glib-2.0 gthread-2.0)

# ---------- runtime ----------
FROM debian:${DEBIAN} AS runtime

ENV DEBIAN_FRONTEND=noninteractive \
    GDK_BACKEND=x11 \
    NO_AT_BRIDGE=1 \
    XAUTHORITY=/tmp/.docker.xauth

RUN apt-get update && apt-get install -y --no-install-recommends \
        libgtk-4-1 \
        libglib2.0-0 \
        libgl1-mesa-dri \
        fonts-dejavu-core \
        xauth \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 1000 --shell /bin/bash fydelis

COPY --from=build /out/fydelisbot-gui /usr/local/bin/fydelisbot-gui

USER fydelis
ENTRYPOINT ["/usr/local/bin/fydelisbot-gui"]

# ---------- headless ----------
FROM runtime AS headless

USER root
RUN apt-get update && apt-get install -y --no-install-recommends xvfb \
    && rm -rf /var/lib/apt/lists/*
USER fydelis

ENTRYPOINT ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24", \
            "/usr/local/bin/fydelisbot-gui"]

