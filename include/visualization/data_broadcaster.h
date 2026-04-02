#pragma once
#include "core/types.h"
#include "visualization/web_server.h"
#include "core/portfolio.h"
#include "regime/regime_detector.h"
#include "stress/stress_engine.h"

namespace ste {

class DataBroadcaster {
public:
    DataBroadcaster(WebServer& server, Portfolio& portfolio,
                    RegimeDetector& detector, StressEngine& stress);

    // Create and broadcast a visualization frame
    void broadcast(const MarketSnapshot& market);

    // Get frame count
    int frameCount() const { return frame_count_; }

private:
    WebServer& server_;
    Portfolio& portfolio_;
    RegimeDetector& detector_;
    StressEngine& stress_;
    int frame_count_ = 0;
};

} // namespace ste
