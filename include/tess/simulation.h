#pragma once

// Curated facade for queued edits, scheduling, path agents, and render deltas.
// Optional EnTT and Dear ImGui integrations remain explicit opt-in headers.

#include <tess/ecs/adapter.h>
#include <tess/ecs/entity_handle.h>
#include <tess/ops/phase_executor.h>
#include <tess/ops/queued.h>
#include <tess/ops/result_channel.h>
#include <tess/pathfinding.h>
#include <tess/sim/auto_exec.h>
#include <tess/sim/delta_frame.h>
#include <tess/sim/movement.h>
#include <tess/sim/path_agent.h>
#include <tess/sim/path_agent_tick.h>
#include <tess/sim/render_delta.h>
#include <tess/sim/schedule.h>
#include <tess/sim/scheduler.h>
#include <tess/sim/time.h>
