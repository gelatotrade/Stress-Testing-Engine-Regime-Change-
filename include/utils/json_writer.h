#pragma once
#include "core/types.h"
#include <string>
#include <sstream>

namespace ste {

class JsonWriter {
public:
    static std::string toJson(const VisualizationFrame& frame);
    static std::string toJson(const PortfolioState& state);
    static std::string toJson(const RegimeState& state);
    static std::string toJson(const TradingSignal& signal);
    static std::string toJson(const StressResult& result);
    static std::string toJson(const Surface3D& surface);
    static std::string toJson(const Point3D& point);
    static std::string toJson(const Greeks& greeks);

    // Escape string for JSON
    static std::string escape(const std::string& s);

private:
    // Helper for building JSON arrays
    template<typename T>
    static std::string arrayToJson(const std::vector<T>& items) {
        std::ostringstream ss;
        ss << "[";
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) ss << ",";
            ss << toJson(items[i]);
        }
        ss << "]";
        return ss.str();
    }
};

} // namespace ste
