# Pinned Steam Runtime SDK for tess Steam Deck and native Linux builds.
#
# Valve recommends building native Linux games inside a Steam Runtime container
# so host libraries do not leak into the binary. steamrt4 (Debian 13) is the
# recommended runtime for new native Linux games and ships CMake >= 3.28 and a
# C++20 compiler out of the box.
#
# The SDK base image is amd64/i386 only -> on Apple Silicon build and run with
#   --platform linux/amd64   (Docker Desktop + Rosetta).
#
# Immutable steamrt4 SDK manifest resolved from Valve's registry. Override the
# build argument deliberately when auditing and upgrading the runtime.
ARG STEAMRT_IMAGE=registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk@sha256:584939ebd7d2f1eec719e771fdde4ae3bd469ee741c783abb7fe812ddaaf3ee4
FROM ${STEAMRT_IMAGE}

# The pinned SDK already contains Clang 19. Using that bundled compiler keeps
# the toolchain inside the image digest; an apt install would reintroduce live,
# mutable package metadata. Clang defaults to the SDK's libstdc++, preserving
# the runtime ABI. Do not switch to libc++ unless it is bundled statically.
