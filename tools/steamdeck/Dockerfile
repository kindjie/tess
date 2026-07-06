# Steam Runtime SDK + clang, for building tess for the Steam Deck / native Linux.
#
# Valve recommends building native Linux games inside a Steam Runtime container
# so host libraries do not leak into the binary. steamrt4 (Debian 13) is the
# recommended runtime for new native Linux games and ships CMake >= 3.28 and a
# C++20 compiler out of the box.
#
# The SDK base image is amd64/i386 only -> on Apple Silicon build and run with
#   --platform linux/amd64   (Docker Desktop + Rosetta).
#
# Verify the exact SDK image tag before pinning it in CI:
#   https://gitlab.steamos.cloud/steamrt/steamrt4/sdk
# sniper's is confirmed as registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest
# and steamrt4 follows the same pattern.
ARG STEAMRT_IMAGE=registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk:latest
FROM ${STEAMRT_IMAGE}

# tess CI builds with clang; use it here too. clang defaults to -stdlib=libstdc++
# (the C++ runtime the Steam Runtime provides) so binaries stay ABI-compatible.
# Do NOT switch to libc++ unless you statically bundle it.
RUN apt-get update \
 && apt-get install -y --no-install-recommends clang \
 && rm -rf /var/lib/apt/lists/*
