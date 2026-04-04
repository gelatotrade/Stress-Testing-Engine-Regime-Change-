#include "live/live_data_feed.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <array>

// For HTTP requests via popen (portable fallback)
// In production, use libcurl directly
namespace {

std::string execCommand(const std::string& cmd) {
    std::array<char, 4096> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// Simple JSON value extractor (avoids external JSON dependency)
std::string extractJsonValue(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++;

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'))
        pos++;

    if (pos >= json.size()) return "";

    // If string value
    if (json[pos] == '"') {
        auto end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }

    // Numeric or boolean value
    auto end = json.find_first_of(",}]\n", pos);
    if (end == std::string::npos) end = json.size();
    std::string val = json.substr(pos, end - pos);
    // Trim whitespace
    while (!val.empty() && (val.back() == ' ' || val.back() == '\n' || val.back() == '\r'))
        val.pop_back();
    return val;
}

double safeStod(const std::string& s, double fallback = 0.0) {
    if (s.empty() || s == "null" || s == "N/A") return fallback;
    try { return std::stod(s); }
    catch (...) { return fallback; }
}

} // anonymous namespace

namespace ste {

// ============================================================
// Yahoo Finance Provider
// ============================================================

YahooFinanceProvider::YahooFinanceProvider(const std::string& symbol,
                                             const std::string& vix_symbol)
    : symbol_(symbol), vix_symbol_(vix_symbol) {
    // Initialize last snapshot with reasonable defaults
    last_snapshot_.timestamp = 0;
    last_snapshot_.spot_price = 4500.0;
    last_snapshot_.sp500_level = 4500.0;
    last_snapshot_.risk_free_rate = 0.05;
    last_snapshot_.dividend_yield = 0.015;
    last_snapshot_.implied_vol = 0.15;
    last_snapshot_.realized_vol = 0.14;
    last_snapshot_.vix = 15.0;
    last_snapshot_.volume = 3.0e9;
    last_snapshot_.put_call_ratio = 0.70;
    last_snapshot_.credit_spread = 1.0;
    last_snapshot_.yield_curve_slope = 1.5;
}

bool YahooFinanceProvider::connect() {
    std::cout << "  [LiveFeed] Connecting to Yahoo Finance...\n";
    // Test connectivity with a simple fetch
    std::string response = httpGet(
        "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol_ +
        "?interval=1m&range=1d");
    if (response.empty() || response.find("error") != std::string::npos) {
        // Try alternative endpoint
        response = httpGet(
            "https://query2.finance.yahoo.com/v8/finance/chart/" + symbol_ +
            "?interval=1m&range=1d");
    }
    connected_ = !response.empty() && response.find("\"chart\"") != std::string::npos;
    if (connected_) {
        std::cout << "  [LiveFeed] Connected to Yahoo Finance successfully.\n";
    } else {
        std::cerr << "  [LiveFeed] WARNING: Could not connect to Yahoo Finance. "
                  << "Falling back to cached/estimated data.\n";
        // Still mark as connected - we'll use estimated data if API is unreachable
        connected_ = true;
    }
    return connected_;
}

void YahooFinanceProvider::disconnect() {
    connected_ = false;
    std::cout << "  [LiveFeed] Disconnected from Yahoo Finance.\n";
}

std::string YahooFinanceProvider::httpGet(const std::string& url) const {
    // Use curl via popen as portable HTTP client
    // In production, link against libcurl for better performance
    std::string cmd = "curl -s -m 10 --compressed -H 'User-Agent: Mozilla/5.0' '" + url + "' 2>/dev/null";
    return execCommand(cmd);
}

double YahooFinanceProvider::parseQuotePrice(const std::string& json,
                                               const std::string& /*symbol*/) const {
    // Parse Yahoo Finance v8 chart API response
    // Look for regularMarketPrice in meta
    std::string price_str = extractJsonValue(json, "regularMarketPrice");
    if (!price_str.empty()) return safeStod(price_str);

    // Fallback: look for close prices array and take last value
    auto pos = json.find("\"close\"");
    if (pos != std::string::npos) {
        pos = json.find('[', pos);
        if (pos != std::string::npos) {
            auto end = json.find(']', pos);
            if (end != std::string::npos) {
                std::string arr = json.substr(pos + 1, end - pos - 1);
                // Find last non-null value
                double last_val = 0.0;
                std::istringstream iss(arr);
                std::string token;
                while (std::getline(iss, token, ',')) {
                    double v = safeStod(token);
                    if (v > 0) last_val = v;
                }
                if (last_val > 0) return last_val;
            }
        }
    }

    return 0.0;
}

double YahooFinanceProvider::parseQuoteVolume(const std::string& json) const {
    std::string vol_str = extractJsonValue(json, "regularMarketVolume");
    if (!vol_str.empty()) return safeStod(vol_str);
    return 0.0;
}

MarketSnapshot YahooFinanceProvider::fetchLatest() {
    auto now = std::chrono::system_clock::now();
    double timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    // Fetch SP500 price
    std::string sp_response = httpGet(
        "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol_ +
        "?interval=1m&range=1d");
    double sp_price = parseQuotePrice(sp_response, symbol_);
    double sp_volume = parseQuoteVolume(sp_response);

    // Fetch VIX
    std::string vix_response = httpGet(
        "https://query1.finance.yahoo.com/v8/finance/chart/" + vix_symbol_ +
        "?interval=1m&range=1d");
    double vix_price = parseQuotePrice(vix_response, vix_symbol_);

    MarketSnapshot snap = last_snapshot_;
    snap.timestamp = timestamp;

    if (sp_price > 0) {
        snap.spot_price = sp_price;
        snap.sp500_level = sp_price;
    }
    if (vix_price > 0) {
        snap.vix = vix_price;
        snap.implied_vol = vix_price / 100.0;
    }
    if (sp_volume > 0) {
        snap.volume = sp_volume;
    }

    // Derive other fields from available data
    snap.realized_vol = 0.8 * last_snapshot_.realized_vol + 0.2 * snap.implied_vol;

    // Estimate credit spread from VIX level (rough proxy)
    snap.credit_spread = 0.5 + (snap.vix - 12.0) * 0.08;
    snap.credit_spread = std::max(0.3, std::min(8.0, snap.credit_spread));

    // Estimate put/call from VIX
    snap.put_call_ratio = 0.6 + (snap.vix - 12.0) * 0.015;
    snap.put_call_ratio = std::max(0.4, std::min(1.8, snap.put_call_ratio));

    last_snapshot_ = snap;
    return snap;
}

// ============================================================
// Mock Live Provider
// ============================================================

MockLiveProvider::MockLiveProvider(double initial_price, unsigned seed)
    : rng_(seed == 0 ? std::random_device{}() : seed) {
    current_.timestamp = 0;
    current_.spot_price = initial_price;
    current_.sp500_level = initial_price;
    current_.risk_free_rate = 0.05;
    current_.dividend_yield = 0.015;
    current_.implied_vol = 0.15;
    current_.realized_vol = 0.14;
    current_.vix = 15.0;
    current_.volume = 3.0e9;
    current_.put_call_ratio = 0.70;
    current_.credit_spread = 1.0;
    current_.yield_curve_slope = 1.5;
    last_fetch_ = std::chrono::steady_clock::now();
}

bool MockLiveProvider::connect() {
    connected_ = true;
    last_fetch_ = std::chrono::steady_clock::now();
    std::cout << "  [LiveFeed] Mock provider connected (real-time simulation).\n";
    return true;
}

void MockLiveProvider::disconnect() {
    connected_ = false;
    std::cout << "  [LiveFeed] Mock provider disconnected.\n";
}

MarketSnapshot MockLiveProvider::fetchLatest() {
    auto now = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(now - last_fetch_).count();
    last_fetch_ = now;

    // Scale movement to elapsed time (annualized params)
    double dt = elapsed_sec / (252.0 * 6.5 * 3600.0); // fraction of trading year

    // Simulate micro-movements with mean-reverting volatility
    double drift = 0.10 * dt;  // ~10% annualized return
    double vol = current_.implied_vol;
    double diffusion = vol * std::sqrt(dt) * normal_(rng_);

    current_.spot_price *= std::exp(drift + diffusion);
    current_.sp500_level = current_.spot_price;

    // VIX mean-reverts to ~16 with noise
    double vix_target = 16.0;
    current_.vix += 0.02 * (vix_target - current_.vix) + normal_(rng_) * 0.3;
    current_.vix = std::max(9.0, std::min(80.0, current_.vix));
    current_.implied_vol = current_.vix / 100.0;
    current_.realized_vol = 0.95 * current_.realized_vol + 0.05 * std::abs(diffusion / std::sqrt(std::max(dt, 1e-10)));

    current_.volume = 3.0e9 * (1.0 + 0.3 * (current_.vix - 15.0) / 15.0 + normal_(rng_) * 0.05);
    current_.put_call_ratio = 0.7 + (current_.vix - 15.0) * 0.01 + normal_(rng_) * 0.02;
    current_.credit_spread = 1.0 + (current_.vix - 15.0) * 0.05 + normal_(rng_) * 0.05;
    current_.credit_spread = std::max(0.3, current_.credit_spread);
    current_.risk_free_rate = std::max(0.01, current_.risk_free_rate + normal_(rng_) * 0.0001);

    auto sys_now = std::chrono::system_clock::now();
    current_.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        sys_now.time_since_epoch()).count();

    tick_count_++;
    return current_;
}

// ============================================================
// LiveDataFeed
// ============================================================

LiveDataFeed::LiveDataFeed(const LiveFeedConfig& config) : config_(config) {}

LiveDataFeed::~LiveDataFeed() {
    stop();
}

void LiveDataFeed::setProvider(std::unique_ptr<IDataProvider> provider) {
    provider_ = std::move(provider);
}

void LiveDataFeed::onData(MarketDataCallback callback) {
    callbacks_.push_back(std::move(callback));
}

std::unique_ptr<IDataProvider> LiveDataFeed::createProvider() const {
    if (config_.api_provider == "yahoo") {
        return std::make_unique<YahooFinanceProvider>(config_.symbol, config_.vix_symbol);
    }
    // Default to mock
    return std::make_unique<MockLiveProvider>();
}

bool LiveDataFeed::start() {
    if (running_.load()) return true;

    if (!provider_) {
        provider_ = createProvider();
    }

    if (!provider_->connect()) {
        std::cerr << "  [LiveFeed] Failed to connect provider.\n";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.provider_name = provider_->providerName();
        stats_.connected = true;
    }

    running_ = true;
    start_time_ = std::chrono::steady_clock::now();
    feed_thread_ = std::thread(&LiveDataFeed::feedLoop, this);

    std::cout << "  [LiveFeed] Started (" << provider_->providerName()
              << ", poll every " << config_.poll_interval_ms << "ms)\n";
    return true;
}

void LiveDataFeed::stop() {
    running_ = false;
    if (feed_thread_.joinable()) {
        feed_thread_.join();
    }
    if (provider_) {
        provider_->disconnect();
    }
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.connected = false;
    }
}

MarketSnapshot LiveDataFeed::latestSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return latest_snapshot_;
}

LiveDataFeed::FeedStats LiveDataFeed::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    FeedStats s = stats_;
    auto now = std::chrono::steady_clock::now();
    s.uptime_seconds = std::chrono::duration<double>(now - start_time_).count();
    return s;
}

void LiveDataFeed::feedLoop() {
    int reconnect_attempts = 0;

    while (running_.load()) {
        try {
            if (!provider_->isConnected()) {
                if (reconnect_attempts >= config_.max_reconnect_attempts) {
                    std::cerr << "  [LiveFeed] Max reconnect attempts reached. Stopping.\n";
                    running_ = false;
                    break;
                }
                std::cout << "  [LiveFeed] Reconnecting (attempt "
                          << reconnect_attempts + 1 << ")...\n";
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(config_.reconnect_delay_ms));
                provider_->connect();
                reconnect_attempts++;
                {
                    std::lock_guard<std::mutex> lock(stats_mutex_);
                    stats_.reconnects++;
                }
                continue;
            }

            reconnect_attempts = 0;
            MarketSnapshot snap = provider_->fetchLatest();

            {
                std::lock_guard<std::mutex> lock(snapshot_mutex_);
                latest_snapshot_ = snap;
            }
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.snapshots_received++;
                stats_.connected = true;
            }

            notifyCallbacks(snap);

        } catch (const std::exception& e) {
            std::cerr << "  [LiveFeed] Error: " << e.what() << "\n";
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.errors++;
        }

        // Wait for next poll
        for (int ms = 0; ms < config_.poll_interval_ms && running_.load(); ms += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void LiveDataFeed::notifyCallbacks(const MarketSnapshot& snapshot) {
    for (const auto& cb : callbacks_) {
        try {
            cb(snapshot);
        } catch (const std::exception& e) {
            std::cerr << "  [LiveFeed] Callback error: " << e.what() << "\n";
        }
    }
}

} // namespace ste
