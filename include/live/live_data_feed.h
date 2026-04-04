#pragma once
#include "core/types.h"
#include <functional>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace ste {

// Configuration for the live data feed
struct LiveFeedConfig {
    std::string api_provider = "yahoo";   // "yahoo", "alphavantage", "iex", "mock"
    std::string api_key;                   // API key for providers that require one
    std::string symbol = "^GSPC";          // SP500 index
    std::string vix_symbol = "^VIX";       // VIX index
    Timeframe timeframe = Timeframe::Minute;
    int poll_interval_ms = 5000;           // how often to poll for new data
    int reconnect_delay_ms = 5000;         // delay before reconnecting on failure
    int max_reconnect_attempts = 10;
};

// Callback for new market data
using MarketDataCallback = std::function<void(const MarketSnapshot&)>;

// Abstract interface for live market data providers
class IDataProvider {
public:
    virtual ~IDataProvider() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual MarketSnapshot fetchLatest() = 0;
    virtual std::string providerName() const = 0;
};

// HTTP-based data provider using Yahoo Finance (no API key needed)
class YahooFinanceProvider : public IDataProvider {
public:
    explicit YahooFinanceProvider(const std::string& symbol = "^GSPC",
                                  const std::string& vix_symbol = "^VIX");
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override { return connected_; }
    MarketSnapshot fetchLatest() override;
    std::string providerName() const override { return "Yahoo Finance"; }

private:
    std::string symbol_;
    std::string vix_symbol_;
    bool connected_ = false;
    MarketSnapshot last_snapshot_;

    // HTTP helper (uses libcurl or system curl)
    std::string httpGet(const std::string& url) const;
    double parseQuotePrice(const std::string& json, const std::string& symbol) const;
    double parseQuoteVolume(const std::string& json) const;
};

// Mock provider for testing - generates realistic data in real-time
class MockLiveProvider : public IDataProvider {
public:
    explicit MockLiveProvider(double initial_price = 4500.0, unsigned seed = 0);
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override { return connected_; }
    MarketSnapshot fetchLatest() override;
    std::string providerName() const override { return "Mock (Real-time Simulation)"; }

private:
    bool connected_ = false;
    MarketSnapshot current_;
    std::mt19937 rng_;
    std::normal_distribution<double> normal_{0.0, 1.0};
    std::chrono::steady_clock::time_point last_fetch_;
    int tick_count_ = 0;
};

// The main live data feed - runs in its own thread, pushes snapshots to callbacks
class LiveDataFeed {
public:
    explicit LiveDataFeed(const LiveFeedConfig& config = {});
    ~LiveDataFeed();

    // Set the data provider
    void setProvider(std::unique_ptr<IDataProvider> provider);

    // Register callback for new data
    void onData(MarketDataCallback callback);

    // Start/stop the feed
    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // Get the latest snapshot (thread-safe)
    MarketSnapshot latestSnapshot() const;

    // Get feed statistics
    struct FeedStats {
        int snapshots_received = 0;
        int errors = 0;
        int reconnects = 0;
        double uptime_seconds = 0.0;
        std::string provider_name;
        bool connected = false;
    };
    FeedStats stats() const;

private:
    LiveFeedConfig config_;
    std::unique_ptr<IDataProvider> provider_;
    std::vector<MarketDataCallback> callbacks_;

    std::thread feed_thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex snapshot_mutex_;
    MarketSnapshot latest_snapshot_;

    mutable std::mutex stats_mutex_;
    FeedStats stats_;
    std::chrono::steady_clock::time_point start_time_;

    void feedLoop();
    void notifyCallbacks(const MarketSnapshot& snapshot);
    std::unique_ptr<IDataProvider> createProvider() const;
};

} // namespace ste
