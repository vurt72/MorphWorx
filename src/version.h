#pragma once

// Single authoritative MorphWorx version definition.
// All Rack and MetaModule builds, manifests, and patch formats
// must derive their version from these macros.

#define MORPHWORX_VERSION_MAJOR 2
#define MORPHWORX_VERSION_MINOR 0
#define MORPHWORX_VERSION_PATCH 1

#define MORPHWORX_STRINGIFY2(x) #x
#define MORPHWORX_STRINGIFY(x) MORPHWORX_STRINGIFY2(x)

#define MORPHWORX_VERSION_STRING \
    MORPHWORX_STRINGIFY(MORPHWORX_VERSION_MAJOR) "." \
    MORPHWORX_STRINGIFY(MORPHWORX_VERSION_MINOR) "." \
    MORPHWORX_STRINGIFY(MORPHWORX_VERSION_PATCH)

// Build identifier used for runtime logging and diagnostics.
#define MORPHWORX_BUILD_ID __DATE__ " " __TIME__
