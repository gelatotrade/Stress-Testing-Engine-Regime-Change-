#include "utils/json_writer.h"
#include <iomanip>
#include <sstream>

namespace ste {

std::string JsonWriter::escape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c;
        }
    }
    return result;
}

static std::string fmtd(double v) {
    std::ostringstream ss;
    ss << std::setprecision(6) << v;
    return ss.str();
}

std::string JsonWriter::toJson(const Point3D& p) {
    return "{\"x\":" + fmtd(p.x) + ",\"y\":" + fmtd(p.y) + ",\"z\":" + fmtd(p.z) +
           ",\"r\":" + fmtd(p.color_r) + ",\"g\":" + fmtd(p.color_g) +
           ",\"b\":" + fmtd(p.color_b) + ",\"a\":" + fmtd(p.color_a) + "}";
}

std::string JsonWriter::toJson(const Greeks& g) {
    return "{\"delta\":" + fmtd(g.delta) + ",\"gamma\":" + fmtd(g.gamma) +
           ",\"theta\":" + fmtd(g.theta) + ",\"vega\":" + fmtd(g.vega) +
           ",\"rho\":" + fmtd(g.rho) + "}";
}

std::string JsonWriter::toJson(const Surface3D& surface) {
    std::ostringstream ss;
    ss << "{\"label\":\"" << escape(surface.label) << "\",\"rows\":" << surface.rows
       << ",\"cols\":" << surface.cols << ",\"grid\":[";
    for (int i = 0; i < surface.rows; ++i) {
        if (i > 0) ss << ",";
        ss << "[";
        for (int j = 0; j < surface.cols; ++j) {
            if (j > 0) ss << ",";
            ss << toJson(surface.grid[i][j]);
        }
        ss << "]";
    }
    ss << "]}";
    return ss.str();
}

std::string JsonWriter::toJson(const PortfolioState& s) {
    std::ostringstream ss;
    ss << "{\"totalValue\":" << fmtd(s.total_value)
       << ",\"cashAlloc\":" << fmtd(s.cash_allocation)
       << ",\"equityAlloc\":" << fmtd(s.equity_allocation)
       << ",\"optionsAlloc\":" << fmtd(s.options_allocation)
       << ",\"delta\":" << fmtd(s.total_delta)
       << ",\"gamma\":" << fmtd(s.total_gamma)
       << ",\"theta\":" << fmtd(s.total_theta)
       << ",\"vega\":" << fmtd(s.total_vega)
       << ",\"var95\":" << fmtd(s.var_95)
       << ",\"cvar95\":" << fmtd(s.cvar_95)
       << ",\"sharpe\":" << fmtd(s.sharpe_ratio)
       << ",\"sortino\":" << fmtd(s.sortino_ratio)
       << ",\"calmar\":" << fmtd(s.calmar_ratio)
       << ",\"maxDrawdown\":" << fmtd(s.max_drawdown)
       << ",\"portfolioReturn\":" << fmtd(s.portfolio_return)
       << ",\"benchmarkReturn\":" << fmtd(s.benchmark_return) << "}";
    return ss.str();
}

static const char* regimeToString(MarketRegime r) {
    switch (r) {
        case MarketRegime::BullQuiet:    return "BullQuiet";
        case MarketRegime::BullVolatile: return "BullVolatile";
        case MarketRegime::BearQuiet:    return "BearQuiet";
        case MarketRegime::BearVolatile: return "BearVolatile";
        case MarketRegime::Transition:   return "Transition";
        default: return "Unknown";
    }
}

std::string JsonWriter::toJson(const RegimeState& s) {
    std::ostringstream ss;
    ss << "{\"regime\":\"" << regimeToString(s.current_regime) << "\""
       << ",\"probabilities\":[";
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ss << ",";
        ss << fmtd(s.regime_probabilities[i]);
    }
    ss << "],\"transitionProb\":" << fmtd(s.transition_probability)
       << ",\"crisisProb\":" << fmtd(s.crisis_probability)
       << ",\"duration\":" << s.regime_duration_days
       << ",\"confidence\":" << fmtd(s.confidence) << "}";
    return ss.str();
}

static const char* signalToString(SignalType s) {
    switch (s) {
        case SignalType::StrongBuy:   return "StrongBuy";
        case SignalType::Buy:         return "Buy";
        case SignalType::Hold:        return "Hold";
        case SignalType::ReduceRisk:  return "ReduceRisk";
        case SignalType::GoToCash:    return "GoToCash";
        case SignalType::Crisis:      return "Crisis";
        default: return "Unknown";
    }
}

std::string JsonWriter::toJson(const TradingSignal& s) {
    std::ostringstream ss;
    ss << "{\"signal\":\"" << signalToString(s.signal) << "\""
       << ",\"confidence\":" << fmtd(s.confidence)
       << ",\"cashTarget\":" << fmtd(s.target_cash_pct)
       << ",\"equityTarget\":" << fmtd(s.target_equity_pct)
       << ",\"optionsTarget\":" << fmtd(s.target_options_pct)
       << ",\"reason\":\"" << escape(s.reason) << "\""
       << ",\"timestamp\":" << fmtd(s.timestamp) << "}";
    return ss.str();
}

std::string JsonWriter::toJson(const StressResult& r) {
    std::ostringstream ss;
    ss << "{\"scenario\":\"" << escape(r.scenario_name) << "\""
       << ",\"pnl\":" << fmtd(r.portfolio_pnl)
       << ",\"pnlPct\":" << fmtd(r.portfolio_pnl_pct)
       << ",\"worstLegPnl\":" << fmtd(r.worst_leg_pnl)
       << ",\"worstLeg\":\"" << escape(r.worst_leg_name) << "\""
       << ",\"greeks\":" << toJson(r.portfolio_greeks)
       << ",\"varImpact\":" << fmtd(r.var_impact)
       << ",\"marginImpact\":" << fmtd(r.margin_impact) << "}";
    return ss.str();
}

std::string JsonWriter::toJson(const VisualizationFrame& frame) {
    std::ostringstream ss;
    ss << "{\"timestamp\":" << fmtd(frame.timestamp)
       << ",\"regime\":\"" << regimeToString(frame.regime) << "\""
       << ",\"portfolio\":" << toJson(frame.portfolio)
       << ",\"signal\":" << toJson(frame.signal)
       << ",\"regimeState\":" << toJson(frame.regime_state)
       << ",\"surfaces\":[";
    for (size_t i = 0; i < frame.surfaces.size(); ++i) {
        if (i > 0) ss << ",";
        ss << toJson(frame.surfaces[i]);
    }
    ss << "],\"stressResults\":[";
    for (size_t i = 0; i < frame.stress_results.size(); ++i) {
        if (i > 0) ss << ",";
        ss << toJson(frame.stress_results[i]);
    }
    ss << "]}";
    return ss.str();
}

} // namespace ste
