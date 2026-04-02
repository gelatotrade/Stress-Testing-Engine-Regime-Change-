#pragma once
#include "core/types.h"
#include <string>
#include <fstream>

namespace ste {

class CsvParser {
public:
    static std::vector<MarketSnapshot> loadMarketData(const std::string& filename);
    static void saveMarketData(const std::string& filename,
                                const std::vector<MarketSnapshot>& data);
    static void saveResults(const std::string& filename,
                             const std::vector<StressResult>& results);
};

} // namespace ste
