# OWNERSHIP=Claude
# The browser GUI as a container (#110; docs/DEPLOY.md): the web server, the
# UCI engine it starts, the page and the net. Configured at run time by the
# GUI_* variables of #109 only; nothing secret is ever built in.
#
#   docker build --build-arg ENGINE_COMMIT=<hash> --build-arg APP_VERSION=<version> -t ascaniusfish:<version> .
#
# Both stages are the same Debian release, so the binaries find the C++
# runtime they were linked against. The Makefile's flags have no -march=native:
# the image runs on any x86-64 server, not only the one it was built on.

FROM debian:trixie-slim AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ gcc make \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
# There is no repository in here, so the commit comes from outside.
ARG ENGINE_COMMIT=unknown
ARG APP_VERSION=dev
# -j2: the two big compiles side by side, and no more of them on a small machine.
RUN make -j2 ascaniusfish_uci gui/ascaniusfish_gui \
         ENGINE_COMMIT="$ENGINE_COMMIT" APP_VERSION="$APP_VERSION"

FROM debian:trixie-slim
ARG ENGINE_COMMIT=unknown
ARG APP_VERSION=dev
LABEL org.opencontainers.image.title="ascaniusfish-gui" \
      org.opencontainers.image.version="$APP_VERSION" \
      org.opencontainers.image.revision="$ENGINE_COMMIT"
# A user of its own, and /data its home: the one place it writes (the kept
# searches, ~/.ascaniusfish/ptt.txt), a folder on the host bind-mounted there.
RUN useradd --uid 10001 --user-group --home-dir /data --no-create-home --shell /usr/sbin/nologin ascaniusfish \
 && mkdir /data && chown ascaniusfish:ascaniusfish /data
# The server finds the engine at ./ascaniusfish_uci and the page next to
# itself in web/; the engine finds nets/nne_d6.bin in the working directory.
WORKDIR /app
COPY --from=build /src/ascaniusfish_uci /app/
COPY --from=build /src/gui/ascaniusfish_gui /app/gui/
COPY --from=build /src/gui/web /app/gui/web
COPY --from=build /src/nets/nne_d6.bin /app/nets/
ENV HOME=/data
USER 10001:10001
EXPOSE 8173
# 0.0.0.0 inside the container; the host publishes the port on 127.0.0.1
# only, for the reverse proxy in front (compose.yaml).
CMD ["/app/gui/ascaniusfish_gui", "bind=0.0.0.0", "port=8173"]
