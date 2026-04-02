#include "visualization/data_broadcaster.h"
#include "utils/json_writer.h"

namespace ste {

DataBroadcaster::DataBroadcaster(WebServer& server, Portfolio& portfolio,
                                   RegimeDetector& detector, StressEngine& stress)
    : server_(server), portfolio_(portfolio), detector_(detector), stress_(stress) {}

void DataBroadcaster::broadcast(const MarketSnapshot& market) {
    VisualizationFrame frame;
    frame.timestamp = market.timestamp;

    // Get regime state
    frame.regime_state = detector_.currentState();
    frame.regime = frame.regime_state.current_regime;

    // Portfolio state
    frame.portfolio = portfolio_.computeState(market);

    // Trading signal
    frame.signal = detector_.generateSignal(frame.regime_state, market);

    // 3D P&L surface (use smaller grid for performance)
    frame.surfaces.push_back(portfolio_.computePnLSurface(market, 0.15, 0.25, 30));

    // Stress test results
    frame.stress_results = stress_.runAllScenarios(portfolio_, market);

    // Serialize and broadcast
    std::string json = JsonWriter::toJson(frame);
    server_.broadcastJson(json);

    frame_count_++;
}

} // namespace ste
