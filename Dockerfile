# Builds a fully static ttymidi binary for ARM Linux (e.g. Raspberry Pi OS).
#
# Alpine/musl is used for a tiny toolchain, but the resulting binary is linked
# fully static (musl + argp + ALSA), so it runs on ANY ARM Linux of the target
# architecture -- including glibc-based Raspbian -- with no runtime deps.
#
# ALSA ships no static archive on Alpine, so libasound.a is built from source.
# Build with: make docker-arm   (see Makefile / README).

FROM alpine:3.20 AS build

RUN apk add --no-cache gcc musl-dev linux-headers argp-standalone make bzip2 wget

# Build a static libasound.a from source (Alpine only packages the shared lib).
# Kept as its own layer so it stays cached across source changes.
ARG ALSA_VERSION=1.2.11
RUN wget -q "https://www.alsa-project.org/files/pub/lib/alsa-lib-${ALSA_VERSION}.tar.bz2" \
    && tar xf "alsa-lib-${ALSA_VERSION}.tar.bz2" \
    && cd "alsa-lib-${ALSA_VERSION}" \
    && ./configure --enable-static --disable-shared --disable-python --without-debug \
    && make -j"$(nproc)" \
    && make install \
    && cd / && rm -rf "/alsa-lib-${ALSA_VERSION}" "/alsa-lib-${ALSA_VERSION}.tar.bz2"

COPY src/ /build/src/
# Version string is computed on the host (this image has no .git) and passed in
# via --build-arg by `make docker-arm`; defaults to "unknown" for a bare build.
ARG TTYMIDI_VERSION=unknown
RUN gcc -O2 -static -s -DTTYMIDI_VERSION="\"$TTYMIDI_VERSION\"" \
    /build/src/*.c -o /ttymidi -largp -lasound -lpthread

# Export stage: `--output` copies just the binary out to the host.
FROM scratch AS export
COPY --from=build /ttymidi /ttymidi
