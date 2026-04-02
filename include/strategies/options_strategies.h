#pragma once
#include "core/types.h"

namespace ste {

class OptionsStrategies {
public:
    // Standard strategies factory methods
    static Strategy createCoveredCall(double spot, double strike, double expiry,
                                      double vol, double rate, int shares = 100);

    static Strategy createProtectivePut(double spot, double strike, double expiry,
                                         double vol, double rate, int shares = 100);

    static Strategy createBullCallSpread(double spot, double lower_strike,
                                          double upper_strike, double expiry,
                                          double vol, double rate);

    static Strategy createBearPutSpread(double spot, double upper_strike,
                                         double lower_strike, double expiry,
                                         double vol, double rate);

    static Strategy createIronCondor(double spot, double put_lower, double put_upper,
                                      double call_lower, double call_upper,
                                      double expiry, double vol, double rate);

    static Strategy createIronButterfly(double spot, double center_strike,
                                         double wing_width, double expiry,
                                         double vol, double rate);

    static Strategy createStraddle(double spot, double strike, double expiry,
                                    double vol, double rate, bool long_pos = true);

    static Strategy createStrangle(double spot, double put_strike, double call_strike,
                                    double expiry, double vol, double rate,
                                    bool long_pos = true);

    static Strategy createCollar(double spot, double put_strike, double call_strike,
                                  double expiry, double vol, double rate, int shares = 100);

    static Strategy createCalendarSpread(double spot, double strike,
                                          double near_expiry, double far_expiry,
                                          double vol, double rate, OptionType type = OptionType::Call);

    // Compute strategy P&L at a given spot price
    static double strategyPnL(const Strategy& strat, double new_spot,
                               double vol, double rate, double time_remaining);

    // Compute strategy breakevens
    static void computeBreakevens(Strategy& strat, double spot, double vol,
                                   double rate);
};

} // namespace ste
