#include "execution/execution_engine.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace ste {

// ============================================================
// Paper Trading Engine
// ============================================================

PaperTradingEngine::PaperTradingEngine(double initial_capital,
                                         double commission_per_trade,
                                         double slippage_bps)
    : initial_capital_(initial_capital),
      cash_(initial_capital),
      commission_per_trade_(commission_per_trade),
      slippage_bps_(slippage_bps) {}

bool PaperTradingEngine::connect() {
    connected_ = true;
    std::cout << "  [Execution] Paper trading engine connected. "
              << "Capital: $" << std::fixed << std::setprecision(0) << cash_ << "\n";
    return true;
}

void PaperTradingEngine::disconnect() {
    connected_ = false;
    auto stats = paperStats();
    std::cout << "  [Execution] Paper trading engine disconnected.\n"
              << "  Total P&L: $" << std::fixed << std::setprecision(2) << stats.total_pnl
              << " | Trades: " << stats.total_trades
              << " | Win Rate: " << std::setprecision(1) << stats.win_rate * 100.0 << "%\n";
}

int PaperTradingEngine::submitOrder(const Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);

    Order o = order;
    o.order_id = next_order_id_++;
    o.status = OrderStatus::Pending;

    if (o.type == OrderType::Market) {
        executeMarketOrder(o);
    } else {
        // For non-market orders, store as pending
        orders_[o.order_id] = o;
        o.status = OrderStatus::Submitted;
        notifyOrderUpdate(o);
    }

    return o.order_id;
}

void PaperTradingEngine::executeMarketOrder(Order& order) {
    // Simulate fill with slippage
    double slippage_mult = 1.0;
    if (order.side == OrderSide::Buy) {
        slippage_mult = 1.0 + slippage_bps_ / 10000.0;
    } else {
        slippage_mult = 1.0 - slippage_bps_ / 10000.0;
    }

    // Get market price (use limit_price as reference if no market price available)
    double market_price = order.limit_price;
    auto it = market_prices_.find(order.symbol);
    if (it != market_prices_.end()) {
        market_price = it->second;
    }

    order.filled_price = market_price * slippage_mult;
    order.filled_quantity = order.quantity;
    order.commission = commission_per_trade_;

    double trade_value = order.filled_price * order.filled_quantity;

    if (order.side == OrderSide::Buy) {
        double cost = trade_value + order.commission;
        if (cost > cash_) {
            // Reduce quantity to fit available cash
            order.filled_quantity = (cash_ - order.commission) / order.filled_price;
            if (order.filled_quantity <= 0) {
                order.status = OrderStatus::Rejected;
                order.filled_quantity = 0;
                orders_[order.order_id] = order;
                notifyOrderUpdate(order);
                return;
            }
            trade_value = order.filled_price * order.filled_quantity;
            cost = trade_value + order.commission;
        }
        cash_ -= cost;

        // Update position
        auto& pos = positions_[order.symbol];
        pos.symbol = order.symbol;
        pos.asset_class = order.asset_class;
        double old_value = pos.avg_price * pos.quantity;
        pos.quantity += order.filled_quantity;
        if (pos.quantity > 0) {
            pos.avg_price = (old_value + trade_value) / pos.quantity;
        }

    } else { // Sell
        auto& pos = positions_[order.symbol];
        if (pos.quantity < order.filled_quantity) {
            order.filled_quantity = pos.quantity; // Can only sell what we have
        }
        if (order.filled_quantity <= 0) {
            order.status = OrderStatus::Rejected;
            order.filled_quantity = 0;
            orders_[order.order_id] = order;
            notifyOrderUpdate(order);
            return;
        }

        trade_value = order.filled_price * order.filled_quantity;
        double cost_basis = pos.avg_price * order.filled_quantity;
        double realized = trade_value - cost_basis;

        cash_ += trade_value - order.commission;
        pos.realized_pnl += realized;
        pos.quantity -= order.filled_quantity;

        if (pos.quantity <= 0.001) {
            positions_.erase(order.symbol);
        }
    }

    order.status = OrderStatus::Filled;
    orders_[order.order_id] = order;
    trade_history_.push_back(order);

    notifyOrderUpdate(order);
    notifyFill(order);
}

bool PaperTradingEngine::cancelOrder(int order_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = orders_.find(order_id);
    if (it == orders_.end()) return false;
    if (it->second.status == OrderStatus::Filled) return false;
    it->second.status = OrderStatus::Cancelled;
    notifyOrderUpdate(it->second);
    return true;
}

Order PaperTradingEngine::getOrderStatus(int order_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = orders_.find(order_id);
    if (it != orders_.end()) return it->second;
    Order empty;
    empty.order_id = -1;
    return empty;
}

std::vector<Order> PaperTradingEngine::openOrders() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Order> open;
    for (const auto& [id, order] : orders_) {
        if (order.status == OrderStatus::Pending || order.status == OrderStatus::Submitted) {
            open.push_back(order);
        }
    }
    return open;
}

AccountState PaperTradingEngine::accountState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    AccountState state;
    state.cash = cash_;
    state.total_equity = cash_;

    for (const auto& [sym, pos] : positions_) {
        Position p = pos;
        auto pit = market_prices_.find(sym);
        if (pit != market_prices_.end()) {
            p.market_value = p.quantity * pit->second;
            p.unrealized_pnl = p.market_value - (p.quantity * p.avg_price);
        } else {
            p.market_value = p.quantity * p.avg_price;
        }
        state.total_equity += p.market_value;
        state.positions.push_back(p);
    }

    state.buying_power = state.cash;
    state.open_orders = 0;
    for (const auto& [id, order] : orders_) {
        if (order.status == OrderStatus::Pending || order.status == OrderStatus::Submitted)
            state.open_orders++;
    }

    return state;
}

std::vector<Position> PaperTradingEngine::positions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Position> result;
    for (const auto& [sym, pos] : positions_) {
        result.push_back(pos);
    }
    return result;
}

void PaperTradingEngine::onOrderUpdate(OrderCallback callback) {
    order_callbacks_.push_back(std::move(callback));
}

void PaperTradingEngine::onFill(FillCallback callback) {
    fill_callbacks_.push_back(std::move(callback));
}

void PaperTradingEngine::updateMarketPrice(const std::string& symbol, double price) {
    std::lock_guard<std::mutex> lock(mutex_);
    market_prices_[symbol] = price;

    // Update position market values
    auto it = positions_.find(symbol);
    if (it != positions_.end()) {
        it->second.market_value = it->second.quantity * price;
        it->second.unrealized_pnl = it->second.market_value -
                                     (it->second.quantity * it->second.avg_price);
    }
}

PaperTradingEngine::PaperStats PaperTradingEngine::paperStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    PaperStats stats;

    double total_equity = cash_;
    for (const auto& [sym, pos] : positions_) {
        auto pit = market_prices_.find(sym);
        if (pit != market_prices_.end()) {
            total_equity += pos.quantity * pit->second;
        } else {
            total_equity += pos.quantity * pos.avg_price;
        }
    }

    stats.total_pnl = total_equity - initial_capital_;
    stats.total_trades = static_cast<int>(trade_history_.size());
    stats.total_commissions = stats.total_trades * commission_per_trade_;

    for (const auto& trade : trade_history_) {
        double pnl = 0;
        if (trade.side == OrderSide::Sell) {
            // Find cost basis - approximate
            pnl = (trade.filled_price - trade.limit_price) * trade.filled_quantity;
            if (pnl > 0) {
                stats.winning_trades++;
                stats.largest_win = std::max(stats.largest_win, pnl);
            } else {
                stats.losing_trades++;
                stats.largest_loss = std::min(stats.largest_loss, pnl);
            }
        }
    }

    if (stats.total_trades > 0) {
        stats.win_rate = static_cast<double>(stats.winning_trades) / stats.total_trades;
    }

    return stats;
}

void PaperTradingEngine::notifyOrderUpdate(const Order& order) {
    for (const auto& cb : order_callbacks_) {
        try { cb(order); } catch (...) {}
    }
}

void PaperTradingEngine::notifyFill(const Order& order) {
    for (const auto& cb : fill_callbacks_) {
        try { cb(order); } catch (...) {}
    }
}

// ============================================================
// Order Manager
// ============================================================

OrderManager::OrderManager(IExecutionEngine& engine, const OrderManagerConfig& config)
    : engine_(engine), config_(config) {}

OrderManager::AllocationDrift OrderManager::checkDrift(const TradingSignal& signal) const {
    AllocationDrift drift;
    auto account = engine_.accountState();

    double total = account.total_equity;
    if (total <= 0) return drift;

    drift.target_cash = signal.target_cash_pct;
    drift.target_equity = signal.target_equity_pct;
    drift.target_options = signal.target_options_pct;
    drift.actual_cash = account.cash / total;

    double equity_value = 0;
    double options_value = 0;
    for (const auto& pos : account.positions) {
        if (pos.asset_class == AssetClass::Option) {
            options_value += std::abs(pos.market_value);
        } else {
            equity_value += std::abs(pos.market_value);
        }
    }
    drift.actual_equity = equity_value / total;
    drift.actual_options = options_value / total;

    drift.needs_rebalance =
        std::abs(drift.target_cash - drift.actual_cash) > config_.rebalance_threshold ||
        std::abs(drift.target_equity - drift.actual_equity) > config_.rebalance_threshold;

    return drift;
}

OrderManager::OrderBatch OrderManager::processSignal(const TradingSignal& signal,
                                                       const MarketSnapshot& market,
                                                       const RegimeState& regime) {
    OrderBatch batch;

    // Check if signal has changed meaningfully
    if (has_last_signal_ && last_signal_.signal == signal.signal) {
        auto drift = checkDrift(signal);
        if (!drift.needs_rebalance) {
            batch.rationale = "No rebalance needed - allocation within threshold.";
            return batch;
        }
    }

    auto account = engine_.accountState();
    double total_equity = account.total_equity;
    if (total_equity <= 0) {
        batch.rationale = "No equity available for trading.";
        return batch;
    }

    double target_equity_dollars = signal.target_equity_pct * total_equity;
    double target_cash_dollars = signal.target_cash_pct * total_equity;

    // Current equity position value
    double current_equity_value = 0;
    double current_equity_shares = 0;
    for (const auto& pos : account.positions) {
        if (pos.symbol == config_.equity_symbol && pos.asset_class != AssetClass::Option) {
            current_equity_value = pos.market_value;
            current_equity_shares = pos.quantity;
        }
    }

    double equity_delta = target_equity_dollars - current_equity_value;

    // Only trade if the delta exceeds minimum trade size
    if (std::abs(equity_delta) > config_.min_trade_size && market.spot_price > 0) {
        Order order;
        order.symbol = config_.equity_symbol;
        order.asset_class = AssetClass::ETF;
        order.type = OrderType::Market;
        order.limit_price = market.spot_price; // reference price
        order.timestamp = market.timestamp;

        // Use SPY price (roughly SP500 / 10)
        double spy_price = market.spot_price / 10.0;
        double shares = std::abs(equity_delta) / spy_price;
        shares = std::floor(shares); // whole shares only

        if (shares >= 1.0) {
            if (equity_delta > 0) {
                order.side = OrderSide::Buy;
                order.quantity = shares;
                order.reason = "Increase equity exposure: " + signal.reason;
            } else {
                order.side = OrderSide::Sell;
                order.quantity = std::min(shares, current_equity_shares);
                order.reason = "Reduce equity exposure: " + signal.reason;
            }
            order.strategy_tag = "regime_allocation";
            batch.orders.push_back(order);
            batch.estimated_turnover += shares * spy_price;
        }
    }

    // Build rationale
    const char* signal_names[] = {"STRONG BUY", "BUY", "HOLD", "REDUCE RISK", "GO TO CASH", "CRISIS"};
    const char* regime_names[] = {"Bull Quiet", "Bull Volatile", "Bear Quiet", "Bear Volatile", "Transition"};

    std::string sig_name = signal_names[static_cast<int>(signal.signal)];
    std::string reg_name = regime_names[static_cast<int>(regime.current_regime)];

    batch.rationale = "Signal: " + sig_name + " | Regime: " + reg_name +
                      " | Target: Cash " + std::to_string(static_cast<int>(signal.target_cash_pct * 100)) +
                      "% Equity " + std::to_string(static_cast<int>(signal.target_equity_pct * 100)) +
                      "% Options " + std::to_string(static_cast<int>(signal.target_options_pct * 100)) + "%";

    // Record in trade log
    TradeRecord record;
    record.timestamp = market.timestamp;
    record.signal = signal.signal;
    record.regime = regime.current_regime;
    record.action = batch.orders.empty() ? "HOLD" :
                    (batch.orders[0].side == OrderSide::Buy ? "BUY" : "SELL");
    record.amount = batch.estimated_turnover;
    record.reason = batch.rationale;
    trade_log_.push_back(record);

    last_signal_ = signal;
    has_last_signal_ = true;

    return batch;
}

void OrderManager::executeBatch(const OrderBatch& batch) {
    for (const auto& order : batch.orders) {
        int id = engine_.submitOrder(order);
        auto status = engine_.getOrderStatus(id);

        const char* side_str = order.side == OrderSide::Buy ? "BUY" : "SELL";
        const char* status_str = "UNKNOWN";
        switch (status.status) {
            case OrderStatus::Filled: status_str = "FILLED"; break;
            case OrderStatus::Rejected: status_str = "REJECTED"; break;
            case OrderStatus::Pending: status_str = "PENDING"; break;
            case OrderStatus::Submitted: status_str = "SUBMITTED"; break;
            default: break;
        }

        std::cout << "  [Execution] " << side_str << " "
                  << std::fixed << std::setprecision(0)
                  << order.quantity << " " << order.symbol
                  << " @ $" << std::setprecision(2) << status.filled_price
                  << " -> " << status_str << "\n";
    }
}

double OrderManager::computeTargetEquityDollars(const TradingSignal& signal,
                                                  double total_equity) const {
    return signal.target_equity_pct * total_equity *
           std::min(1.0, config_.max_total_exposure);
}

double OrderManager::computeTargetCashDollars(const TradingSignal& signal,
                                                double total_equity) const {
    return signal.target_cash_pct * total_equity;
}

} // namespace ste
