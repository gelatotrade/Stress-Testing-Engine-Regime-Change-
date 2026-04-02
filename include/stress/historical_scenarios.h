#pragma once
#include "core/types.h"

namespace ste {

class HistoricalScenarios {
public:
    // Pre-built historical crisis scenarios
    static StressScenario blackMonday1987();
    static StressScenario dotComBubble2000();
    static StressScenario gfc2008();
    static StressScenario flashCrash2010();
    static StressScenario volmageddon2018();
    static StressScenario covidCrash2020();
    static StressScenario memeStonks2021();
    static StressScenario rateHike2022();

    // Get all historical scenarios
    static std::vector<StressScenario> allScenarios();

    // Custom historical replay
    static std::vector<StressScenario> replayPeriod(
        const std::vector<MarketSnapshot>& history,
        int window_days = 5);
};

} // namespace ste
