#pragma once

// Curated facade for tile worlds, movement rules, topology, and pathfinding.
// Prefer narrower owning headers in compile-sensitive translation units.

#include <tess/core/shape.h>
#include <tess/path/distance_field_box.h>
#include <tess/path/field_product_cache.h>
#include <tess/path/path.h>
#include <tess/path/path_runtime.h>
#include <tess/path/path_view.h>
#include <tess/path/portal_route.h>
#include <tess/path/portal_segment_cache.h>
#include <tess/path/precheck.h>
#include <tess/path/route_cache.h>
#include <tess/storage/residency.h>
#include <tess/storage/sparse_world.h>
#include <tess/storage/world.h>
#include <tess/topology/movement_class.h>
#include <tess/topology/topology.h>
#include <tess/topology/transition_provider.h>
#include <tess/version.h>
