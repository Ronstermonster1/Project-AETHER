// ProjectInfo.h
// Identifying information for the AETHER CPU research simulator.
// Every experiment output records these values so that each result can be
// traced back to the exact version of the simulator that produced it.

#pragma once

#include <string_view>

namespace aether
{
    // Human-readable project name, shown in console output and CSV headers.
    inline constexpr std::string_view kProjectName = "AETHER CPU Research Simulator";

    // Increase this whenever a change could alter experiment results
    // (for example: the cost model, the change detection thresholds, the scenario generator).
    // Format: MAJOR.MINOR.PATCH
    // 0.2.0: added the seeded ScenarioGenerator.
    // 0.3.0: added the ChangeDetector.
    inline constexpr std::string_view kSimulatorVersion = "0.3.0";
}