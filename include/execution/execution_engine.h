#pragma once
#include "core/types.h"
#include <functional>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

namespace ste {

// ============================================================
// Order Types for the Execution Engine
// ============================================================

enum class OrderSide { Buy, Sell };
enum class OrderType { Market, Limit, Stop, StopLimit };
enum class OrderStatus { Pending, Submitted, Filled, PartiallyFilled, Cancelled, Rejected };
enum class AssetClass { Equity, Option, Future, ETF };

struct Order {
    int order_id = 0;
    std::string symbol;
    AssetClass asset_class = AssetClass::Equity;
    OrderSide side;
    OrderType type = OrderType::Market;
    double quantity = 0.0;
    double limit_price = 0.0;        // for limit/stop-limit orders
    double stop_price = 0.0;         // for stop/stop-limit orders
    OrderStatus status = OrderStatus::Pending;
    double filled_price = 0.0;
    double filled_quantity = 0.0;
    double commission = 0.0;
    double timestamp = 0.0;
    std::string strategy_tag;         // which strategy generated this
    std::string reason;               // human-readable reason
};

struct Position {
    std::string symbol;
    AssetClass asset_class = AssetClass::Equity;
    double quantity = 0.0;            // positive=long, negative=short
    double avg_price = 0.0;
    double market_value = 0.0;
    double unrealized_pnl = 0.0;
    double realized_pnl = 0.0;
};

struct AccountState {
    double cash = 0.0;
    double total_equity = 0.0;
    double buying_power = 0.0;
    double margin_used = 0.0;
    std::vector<Position> positions;
    int open_orders = 0;
};

// Callback types
using OrderCallback = std::function<void(const Order&)>;
using FillCallback = std::function<void(const Order&)>;

// ============================================================
// Abstract Execution Engine Interface
// ============================================================

class IExecutionEngine {
public:
    virtual ~IExecutionEngine() = default;

    // Connection
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual std::string engineName() const = 0;

    // Order management
    virtual int submitOrder(const Order& order) = 0;
    virtual bool cancelOrder(int order_id) = 0;
    virtual Order getOrderStatus(int order_id) const = 0;
    virtual std::vector<Order> openOrders() const = 0;

    // Account & positions
    virtual AccountState accountState() const = 0;
    virtual std::vector<Position> positions() const = 0;

    // Callbacks
    virtual void onOrderUpdate(OrderCallback callback) = 0;
    virtual void onFill(FillCallback callback) = 0;
};

// ============================================================
// Paper Trading Engine (simulated execution)
// ============================================================

class PaperTradingEngine : public IExecutionEngine {
public:
    explicit PaperTradingEngine(double initial_capital = 1000000.0,
                                 double commission_per_trade = 1.0,
                                 double slippage_bps = 2.0);

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override { return connected_; }
    std::string engineName() const override { return "Paper Trading"; }

    int submitOrder(const Order& order) override;
    bool cancelOrder(int order_id) override;
    Order getOrderStatus(int order_id) const override;
    std::vector<Order> openOrders() const override;

    AccountState accountState() const override;
    std::vector<Position> positions() const override;

    void onOrderUpdate(OrderCallback callback) override;
    void onFill(FillCallback callback) override;

    // Paper-specific: update market prices for position valuation
    void updateMarketPrice(const std::string& symbol, double price);

    // Get trade history
    const std::vector<Order>& tradeHistory() const { return trade_history_; }

    // Performance metrics
    struct PaperStats {
        double total_pnl = 0.0;
        double total_commissions = 0.0;
        int total_trades = 0;
        int winning_trades = 0;
        int losing_trades = 0;
        double win_rate = 0.0;
        double largest_win = 0.0;
        double largest_loss = 0.0;
    };
    PaperStats paperStats() const;

private:
    bool connected_ = false;
    double initial_capital_;
    double cash_;
    double commission_per_trade_;
    double slippage_bps_;
    int next_order_id_ = 1;

    mutable std::mutex mutex_;
    std::map<int, Order> orders_;
    std::map<std::string, Position> positions_;
    std::map<std::string, double> market_prices_;
    std::vector<Order> trade_history_;

    std::vector<OrderCallback> order_callbacks_;
    std::vector<FillCallback> fill_callbacks_;

    void executeMarketOrder(Order& order);
    void notifyOrderUpdate(const Order& order);
    void notifyFill(const Order& order);
};

// ============================================================
// Order Manager: Converts TradingSignals into Orders
// ============================================================

struct OrderManagerConfig {
    double max_position_pct = 0.10;       // max 10% of portfolio in one position
    double max_total_exposure = 0.95;     // max 95% invested
    double min_trade_size = 100.0;        // minimum trade in dollars
    double rebalance_threshold = 0.05;    // rebalance if allocation drifts >5%
    bool enable_options = true;
    bool enable_short_selling = false;
    std::string equity_symbol = "SPY";    // SP500 ETF proxy
    std::string vix_hedge_symbol = "UVXY"; // VIX hedge instrument
};

class OrderManager {
public:
    explicit OrderManager(IExecutionEngine& engine,
                          const OrderManagerConfig& config = {});

    // Process a new trading signal and generate orders
    struct OrderBatch {
        std::vector<Order> orders;
        std::string rationale;
        double estimated_turnover = 0.0;
    };

    OrderBatch processSignal(const TradingSignal& signal,
                              const MarketSnapshot& market,
                              const RegimeState& regime);

    // Execute the order batch
    void executeBatch(const OrderBatch& batch);

    // Get current target vs actual allocation
    struct AllocationDrift {
        double target_cash = 0.0;
        double actual_cash = 0.0;
        double target_equity = 0.0;
        double actual_equity = 0.0;
        double target_options = 0.0;
        double actual_options = 0.0;
        bool needs_rebalance = false;
    };
    AllocationDrift checkDrift(const TradingSignal& signal) const;

    // Trade log
    struct TradeRecord {
        double timestamp;
        SignalType signal;
        MarketRegime regime;
        std::string action;
        double amount;
        std::string reason;
    };
    const std::vector<TradeRecord>& tradeLog() const { return trade_log_; }

private:
    IExecutionEngine& engine_;
    OrderManagerConfig config_;
    std::vector<TradeRecord> trade_log_;
    TradingSignal last_signal_;
    bool has_last_signal_ = false;

    double computeTargetEquityDollars(const TradingSignal& signal,
                                       double total_equity) const;
    double computeTargetCashDollars(const TradingSignal& signal,
                                     double total_equity) const;
};

} // namespace ste
