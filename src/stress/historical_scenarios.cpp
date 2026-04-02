#include "stress/historical_scenarios.h"

namespace ste {

StressScenario HistoricalScenarios::blackMonday1987() {
    return {"Black Monday 1987", -0.226, 0.40, -0.01, 1, 0.5, 3.0};
}

StressScenario HistoricalScenarios::dotComBubble2000() {
    return {"Dot-Com Crash 2000", -0.45, 0.20, -0.02, 120, 0.3, 1.5};
}

StressScenario HistoricalScenarios::gfc2008() {
    return {"GFC 2008", -0.55, 0.50, -0.03, 90, 0.6, 4.0};
}

StressScenario HistoricalScenarios::flashCrash2010() {
    return {"Flash Crash 2010", -0.09, 0.25, 0.0, 1, 0.4, 5.0};
}

StressScenario HistoricalScenarios::volmageddon2018() {
    return {"Volmageddon 2018", -0.10, 0.35, 0.005, 3, 0.3, 2.5};
}

StressScenario HistoricalScenarios::covidCrash2020() {
    return {"COVID Crash 2020", -0.34, 0.55, -0.015, 23, 0.5, 3.5};
}

StressScenario HistoricalScenarios::memeStonks2021() {
    return {"Meme Stocks 2021", 0.15, 0.30, 0.0, 5, 0.2, 2.0};
}

StressScenario HistoricalScenarios::rateHike2022() {
    return {"Rate Hike 2022", -0.25, 0.15, 0.03, 180, 0.2, 1.5};
}

std::vector<StressScenario> HistoricalScenarios::allScenarios() {
    return {
        blackMonday1987(),
        dotComBubble2000(),
        gfc2008(),
        flashCrash2010(),
        volmageddon2018(),
        covidCrash2020(),
        memeStonks2021(),
        rateHike2022()
    };
}

std::vector<StressScenario> HistoricalScenarios::replayPeriod(
    const std::vector<MarketSnapshot>& history, int window_days) {
    std::vector<StressScenario> scenarios;

    for (size_t i = window_days; i < history.size(); i += window_days) {
        double spot_change = (history[i].spot_price - history[i - window_days].spot_price)
                           / history[i - window_days].spot_price;
        double vol_change = history[i].implied_vol - history[i - window_days].implied_vol;
        double rate_change = history[i].risk_free_rate - history[i - window_days].risk_free_rate;

        StressScenario s;
        s.name = "Replay_Day" + std::to_string(i);
        s.spot_shock = spot_change;
        s.vol_shock = vol_change;
        s.rate_shock = rate_change;
        s.time_shock = window_days;
        s.correlation_shock = 0.0;
        s.liquidity_shock = 1.0;
        scenarios.push_back(s);
    }
    return scenarios;
}

} // namespace ste
