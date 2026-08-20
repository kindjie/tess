#pragma once

#include <tess/experimental/registered_maintenance.h>
#include <tess/maintenance/task.h>

namespace tess::maintenance {

/// Outcome of offering one registered task to a scheduler backend.
using ScheduleResult = experimental::maintenance::ScheduleResult;

/// Backend result observed before the registered facade checks pending work.
using BackendDrainResult = experimental::maintenance::BackendDrainResult;

/// Outcome of an explicit budgeted drain or flush.
using DrainResult = experimental::maintenance::DrainResult;

/// Outcome of checked task-registration retirement.
using ReleaseResult = experimental::maintenance::ReleaseResult;

/// Opaque owner, slot, and generation identity for one registration.
using MaintenanceHandle = experimental::maintenance::MaintenanceHandle;

/**
 * Structural scheduler-backend contract used by `RegisteredScheduler`.
 *
 * This is a compile-time customization boundary. It does not make the
 * experimental virtual `MaintenanceScheduler` interface stable.
 */
template <typename Backend>
concept MaintenanceBackend =
    experimental::maintenance::MaintenanceBackend<Backend>;

/// Optional paired fixed-registration hooks for a structural backend.
template <typename Backend>
concept FixedRegistrationBackend =
    experimental::maintenance::FixedRegistrationBackend<Backend>;

/**
 * Fixed-registration scheduler with opaque handles and explicit outcomes.
 *
 * The implementation is shared with the measured pre-graduation candidate;
 * this stable spelling does not grant authority to an experimental backend.
 */
template <typename Backend>
using RegisteredScheduler =
    experimental::maintenance::RegisteredScheduler<Backend>;

/**
 * Synchronous backend for the stable maintenance contract.
 *
 * Use it through `RegisteredScheduler`. The experimental virtual base remains
 * outside the documented stable customization contract.
 */
using ImmediateScheduler = experimental::maintenance::ImmediateScheduler;

}  // namespace tess::maintenance
