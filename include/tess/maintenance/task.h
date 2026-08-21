#pragma once

#include <tess/experimental/maintenance.h>

namespace tess::maintenance {

/// Shared unit budget passed through one maintenance drain.
using MaintenanceBudget = experimental::maintenance::MaintenanceBudget;

/// Long-lived caller-owned derived-state maintenance operation.
using MaintenanceTask = experimental::maintenance::MaintenanceTask;

/// Monotonic scheduling observations for diagnostics.
using MaintenanceMetrics = experimental::maintenance::MaintenanceMetrics;

}  // namespace tess::maintenance
