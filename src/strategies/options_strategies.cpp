#include "strategies/options_strategies.h"
#include "core/black_scholes.h"

namespace ste {

Strategy OptionsStrategies::createCoveredCall(double spot, double strike, double expiry,
                                               double vol, double rate, int shares) {
    Strategy strat;
    strat.type = StrategyType::CoveredCall;
    strat.name = "Covered Call";
    strat.underlying_shares = shares;

    Option call{OptionType::Call, ExerciseStyle::European, strike, expiry, 0.0, -shares/100, spot};
    call.premium = BlackScholes::price(call, spot, vol, rate);

    strat.legs.push_back({call, BlackScholes::computeGreeks(call, spot, vol, rate), 0.0});
    strat.max_profit = (strike - spot) * shares + call.premium * shares;
    strat.max_loss = spot * shares - call.premium * shares;
    strat.breakeven_lower = spot - call.premium;
    strat.breakeven_upper = strike;
    return strat;
}

Strategy OptionsStrategies::createProtectivePut(double spot, double strike, double expiry,
                                                  double vol, double rate, int shares) {
    Strategy strat;
    strat.type = StrategyType::ProtectivePut;
    strat.name = "Protective Put";
    strat.underlying_shares = shares;

    Option put{OptionType::Put, ExerciseStyle::European, strike, expiry, 0.0, shares/100, spot};
    put.premium = BlackScholes::price(put, spot, vol, rate);

    strat.legs.push_back({put, BlackScholes::computeGreeks(put, spot, vol, rate), 0.0});
    strat.max_loss = (spot - strike) * shares + put.premium * shares;
    strat.max_profit = 1e18;  // unlimited
    strat.breakeven_lower = strike;
    strat.breakeven_upper = spot + put.premium;
    return strat;
}

Strategy OptionsStrategies::createBullCallSpread(double spot, double lower_strike,
                                                   double upper_strike, double expiry,
                                                   double vol, double rate) {
    Strategy strat;
    strat.type = StrategyType::BullCallSpread;
    strat.name = "Bull Call Spread";

    Option long_call{OptionType::Call, ExerciseStyle::European, lower_strike, expiry, 0.0, 1, spot};
    long_call.premium = BlackScholes::price(long_call, spot, vol, rate);

    Option short_call{OptionType::Call, ExerciseStyle::European, upper_strike, expiry, 0.0, -1, spot};
    short_call.premium = BlackScholes::price(short_call, spot, vol, rate);

    strat.legs.push_back({long_call, BlackScholes::computeGreeks(long_call, spot, vol, rate), 0.0});
    strat.legs.push_back({short_call, BlackScholes::computeGreeks(short_call, spot, vol, rate), 0.0});

    double net_debit = long_call.premium - short_call.premium;
    strat.max_profit = (upper_strike - lower_strike - net_debit) * 100.0;
    strat.max_loss = net_debit * 100.0;
    strat.breakeven_lower = lower_strike + net_debit;
    strat.breakeven_upper = upper_strike;
    return strat;
}

Strategy OptionsStrategies::createBearPutSpread(double spot, double upper_strike,
                                                  double lower_strike, double expiry,
                                                  double vol, double rate) {
    Strategy strat;
    strat.type = StrategyType::BearPutSpread;
    strat.name = "Bear Put Spread";

    Option long_put{OptionType::Put, ExerciseStyle::European, upper_strike, expiry, 0.0, 1, spot};
    long_put.premium = BlackScholes::price(long_put, spot, vol, rate);

    Option short_put{OptionType::Put, ExerciseStyle::European, lower_strike, expiry, 0.0, -1, spot};
    short_put.premium = BlackScholes::price(short_put, spot, vol, rate);

    strat.legs.push_back({long_put, BlackScholes::computeGreeks(long_put, spot, vol, rate), 0.0});
    strat.legs.push_back({short_put, BlackScholes::computeGreeks(short_put, spot, vol, rate), 0.0});

    double net_debit = long_put.premium - short_put.premium;
    strat.max_profit = (upper_strike - lower_strike - net_debit) * 100.0;
    strat.max_loss = net_debit * 100.0;
    strat.breakeven_lower = lower_strike;
    strat.breakeven_upper = upper_strike - net_debit;
    return strat;
}

Strategy OptionsStrategies::createIronCondor(double spot, double put_lower, double put_upper,
                                               double call_lower, double call_upper,
                                               double expiry, double vol, double rate) {
    Strategy strat;
    strat.type = StrategyType::IronCondor;
    strat.name = "Iron Condor";

    // Bull put spread (lower)
    Option short_put{OptionType::Put, ExerciseStyle::European, put_upper, expiry, 0.0, -1, spot};
    short_put.premium = BlackScholes::price(short_put, spot, vol, rate);
    Option long_put{OptionType::Put, ExerciseStyle::European, put_lower, expiry, 0.0, 1, spot};
    long_put.premium = BlackScholes::price(long_put, spot, vol, rate);

    // Bear call spread (upper)
    Option short_call{OptionType::Call, ExerciseStyle::European, call_lower, expiry, 0.0, -1, spot};
    short_call.premium = BlackScholes::price(short_call, spot, vol, rate);
    Option long_call{OptionType::Call, ExerciseStyle::European, call_upper, expiry, 0.0, 1, spot};
    long_call.premium = BlackScholes::price(long_call, spot, vol, rate);

    strat.legs.push_back({long_put, BlackScholes::computeGreeks(long_put, spot, vol, rate), 0.0});
    strat.legs.push_back({short_put, BlackScholes::computeGreeks(short_put, spot, vol, rate), 0.0});
    strat.legs.push_back({short_call, BlackScholes::computeGreeks(short_call, spot, vol, rate), 0.0});
    strat.legs.push_back({long_call, BlackScholes::computeGreeks(long_call, spot, vol, rate), 0.0});

    double net_credit = (short_put.premium - long_put.premium) + (short_call.premium - long_call.premium);
    double width = std::max(put_upper - put_lower, call_upper - call_lower);
    strat.max_profit = net_credit * 100.0;
    strat.max_loss = (width - net_credit) * 100.0;
    strat.breakeven_lower = put_upper - net_credit;
    strat.breakeven_upper = call_lower + net_credit;
    return strat;
}

Strategy OptionsStrategies::createIronButterfly(double spot, double center_strike,
                                                  double wing_width, double expiry,
                                                  double vol, double rate) {
    Strategy strat;
    strat.type = StrategyType::IronButterfly;
    strat.name = "Iron Butterfly";

    Option short_put{OptionType::Put, ExerciseStyle::European, center_strike, expiry, 0.0, -1, spot};
    short_put.premium = BlackScholes::price(short_put, spot, vol, rate);
    Option short_call{OptionType::Call, ExerciseStyle::European, center_strike, expiry, 0.0, -1, spot};
    short_call.premium = BlackScholes::price(short_call, spot, vol, rate);
    Option long_put{OptionType::Put, ExerciseStyle::European, center_strike - wing_width, expiry, 0.0, 1, spot};
    long_put.premium = BlackScholes::price(long_put, spot, vol, rate);
    Option long_call{OptionType::Call, ExerciseStyle::European, center_strike + wing_width, expiry, 0.0, 1, spot};
    long_call.premium = BlackScholes::price(long_call, spot, vol, rate);

    strat.legs.push_back({long_put, BlackScholes::computeGreeks(long_put, spot, vol, rate), 0.0});
    strat.legs.push_back({short_put, BlackScholes::computeGreeks(short_put, spot, vol, rate), 0.0});
    strat.legs.push_back({short_call, BlackScholes::computeGreeks(short_call, spot, vol, rate), 0.0});
    strat.legs.push_back({long_call, BlackScholes::computeGreeks(long_call, spot, vol, rate), 0.0});

    double net_credit = short_put.premium + short_call.premium - long_put.premium - long_call.premium;
    strat.max_profit = net_credit * 100.0;
    strat.max_loss = (wing_width - net_credit) * 100.0;
    strat.breakeven_lower = center_strike - net_credit;
    strat.breakeven_upper = center_strike + net_credit;
    return strat;
}

Strategy OptionsStrategies::createStraddle(double spot, double strike, double expiry,
                                            double vol, double rate, bool long_pos) {
    Strategy strat;
    strat.type = StrategyType::Straddle;
    strat.name = long_pos ? "Long Straddle" : "Short Straddle";
    int qty = long_pos ? 1 : -1;

    Option call{OptionType::Call, ExerciseStyle::European, strike, expiry, 0.0, qty, spot};
    call.premium = BlackScholes::price(call, spot, vol, rate);
    Option put{OptionType::Put, ExerciseStyle::European, strike, expiry, 0.0, qty, spot};
    put.premium = BlackScholes::price(put, spot, vol, rate);

    strat.legs.push_back({call, BlackScholes::computeGreeks(call, spot, vol, rate), 0.0});
    strat.legs.push_back({put, BlackScholes::computeGreeks(put, spot, vol, rate), 0.0});

    double total_premium = call.premium + put.premium;
    if (long_pos) {
        strat.max_loss = total_premium * 100.0;
        strat.max_profit = 1e18;
    } else {
        strat.max_profit = total_premium * 100.0;
        strat.max_loss = 1e18;
    }
    strat.breakeven_lower = strike - total_premium;
    strat.breakeven_upper = strike + total_premium;
    return strat;
}

Strategy OptionsStrategies::createStrangle(double spot, double put_strike, double call_strike,
                                            double expiry, double vol, double rate,
                                            bool long_pos) {
    Strategy strat;
    strat.type = StrategyType::Strangle;
    strat.name = long_pos ? "Long Strangle" : "Short Strangle";
    int qty = long_pos ? 1 : -1;

    Option call{OptionType::Call, ExerciseStyle::European, call_strike, expiry, 0.0, qty, spot};
    call.premium = BlackScholes::price(call, spot, vol, rate);
    Option put{OptionType::Put, ExerciseStyle::European, put_strike, expiry, 0.0, qty, spot};
    put.premium = BlackScholes::price(put, spot, vol, rate);

    strat.legs.push_back({put, BlackScholes::computeGreeks(put, spot, vol, rate), 0.0});
    strat.legs.push_back({call, BlackScholes::computeGreeks(call, spot, vol, rate), 0.0});

    double total_premium = call.premium + put.premium;
    strat.breakeven_lower = put_strike - total_premium;
    strat.breakeven_upper = call_strike + total_premium;
    if (long_pos) {
        strat.max_loss = total_premium * 100.0;
        strat.max_profit = 1e18;
    } else {
        strat.max_profit = total_premium * 100.0;
        strat.max_loss = 1e18;
    }
    return strat;
}

Strategy OptionsStrategies::createCollar(double spot, double put_strike, double call_strike,
                                          double expiry, double vol, double rate, int shares) {
    Strategy strat;
    strat.type = StrategyType::Collar;
    strat.name = "Collar";
    strat.underlying_shares = shares;

    Option long_put{OptionType::Put, ExerciseStyle::European, put_strike, expiry, 0.0, shares/100, spot};
    long_put.premium = BlackScholes::price(long_put, spot, vol, rate);
    Option short_call{OptionType::Call, ExerciseStyle::European, call_strike, expiry, 0.0, -(shares/100), spot};
    short_call.premium = BlackScholes::price(short_call, spot, vol, rate);

    strat.legs.push_back({long_put, BlackScholes::computeGreeks(long_put, spot, vol, rate), 0.0});
    strat.legs.push_back({short_call, BlackScholes::computeGreeks(short_call, spot, vol, rate), 0.0});

    strat.max_profit = (call_strike - spot) * shares;
    strat.max_loss = (spot - put_strike) * shares;
    strat.breakeven_lower = put_strike;
    strat.breakeven_upper = call_strike;
    return strat;
}

Strategy OptionsStrategies::createCalendarSpread(double spot, double strike,
                                                   double near_expiry, double far_expiry,
                                                   double vol, double rate, OptionType type) {
    Strategy strat;
    strat.type = StrategyType::CalendarSpread;
    strat.name = "Calendar Spread";

    Option short_opt{type, ExerciseStyle::European, strike, near_expiry, 0.0, -1, spot};
    short_opt.premium = BlackScholes::price(short_opt, spot, vol, rate);
    Option long_opt{type, ExerciseStyle::European, strike, far_expiry, 0.0, 1, spot};
    long_opt.premium = BlackScholes::price(long_opt, spot, vol, rate);

    strat.legs.push_back({short_opt, BlackScholes::computeGreeks(short_opt, spot, vol, rate), 0.0});
    strat.legs.push_back({long_opt, BlackScholes::computeGreeks(long_opt, spot, vol, rate), 0.0});

    double net_debit = long_opt.premium - short_opt.premium;
    strat.max_loss = net_debit * 100.0;
    strat.max_profit = 0.0;  // complex, depends on vol
    strat.breakeven_lower = strike - net_debit;
    strat.breakeven_upper = strike + net_debit;
    return strat;
}

double OptionsStrategies::strategyPnL(const Strategy& strat, double new_spot,
                                       double vol, double rate, double time_remaining) {
    double pnl = 0.0;
    for (const auto& leg : strat.legs) {
        Option shifted = leg.option;
        shifted.expiry = time_remaining;
        double current = BlackScholes::price(shifted, new_spot, vol, rate);
        pnl += (current - leg.option.premium) * leg.option.quantity * 100.0;
    }
    pnl += strat.underlying_shares * (new_spot - strat.legs[0].option.underlying_price);
    return pnl;
}

void OptionsStrategies::computeBreakevens(Strategy& strat, double spot, double vol,
                                           double rate) {
    // Numerical search for breakevens
    double lower = spot * 0.5;
    double upper = spot * 1.5;
    double time_rem = strat.legs.empty() ? 0.0 : strat.legs[0].option.expiry;

    // Find lower breakeven
    for (double s = spot; s > lower; s -= 0.1) {
        if (strategyPnL(strat, s, vol, rate, time_rem) < 0) {
            strat.breakeven_lower = s;
            break;
        }
    }
    // Find upper breakeven
    for (double s = spot; s < upper; s += 0.1) {
        if (strategyPnL(strat, s, vol, rate, time_rem) < 0) {
            strat.breakeven_upper = s;
            break;
        }
    }
}

} // namespace ste
