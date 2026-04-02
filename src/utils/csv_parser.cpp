#include "utils/csv_parser.h"
#include <sstream>
#include <iostream>

namespace ste {

std::vector<MarketSnapshot> CsvParser::loadMarketData(const std::string& filename) {
    std::vector<MarketSnapshot> data;
    std::ifstream file(filename);
    if (!file.is_open()) return data;

    std::string line;
    std::getline(file, line);  // skip header

    while (std::getline(file, line)) {
        std::istringstream ss(line);
        MarketSnapshot snap;
        char comma;
        ss >> snap.timestamp >> comma
           >> snap.spot_price >> comma
           >> snap.risk_free_rate >> comma
           >> snap.dividend_yield >> comma
           >> snap.implied_vol >> comma
           >> snap.realized_vol >> comma
           >> snap.vix >> comma
           >> snap.sp500_level >> comma
           >> snap.volume >> comma
           >> snap.put_call_ratio >> comma
           >> snap.credit_spread >> comma
           >> snap.yield_curve_slope;
        data.push_back(snap);
    }
    return data;
}

void CsvParser::saveMarketData(const std::string& filename,
                                const std::vector<MarketSnapshot>& data) {
    std::ofstream file(filename);
    if (!file.is_open()) return;

    file << "timestamp,spot_price,risk_free_rate,dividend_yield,implied_vol,"
         << "realized_vol,vix,sp500_level,volume,put_call_ratio,credit_spread,"
         << "yield_curve_slope\n";

    for (const auto& s : data) {
        file << s.timestamp << "," << s.spot_price << "," << s.risk_free_rate << ","
             << s.dividend_yield << "," << s.implied_vol << "," << s.realized_vol << ","
             << s.vix << "," << s.sp500_level << "," << s.volume << ","
             << s.put_call_ratio << "," << s.credit_spread << ","
             << s.yield_curve_slope << "\n";
    }
}

void CsvParser::saveResults(const std::string& filename,
                             const std::vector<StressResult>& results) {
    std::ofstream file(filename);
    if (!file.is_open()) return;

    file << "scenario,pnl,pnl_pct,worst_leg_pnl,worst_leg,delta,gamma,theta,vega\n";
    for (const auto& r : results) {
        file << r.scenario_name << "," << r.portfolio_pnl << "," << r.portfolio_pnl_pct << ","
             << r.worst_leg_pnl << "," << r.worst_leg_name << ","
             << r.portfolio_greeks.delta << "," << r.portfolio_greeks.gamma << ","
             << r.portfolio_greeks.theta << "," << r.portfolio_greeks.vega << "\n";
    }
}

} // namespace ste
