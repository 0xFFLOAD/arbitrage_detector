#include "arbitrage_detector.h"
#include "../shared/config.h"
#include <iostream>

ArbitrageDetector::ArbitrageDetector(PriceStorage& storage) : storage_(storage) {}

void ArbitrageDetector::onPriceUpdate(Exchange exc, Symbol sym, Price price) {
    /*
        1. Get the price from the OTHER exchange for the same symbol
        - If the other exchange has no price yet (returns -1.0), bail out — can't compare
        2. Figure out which exchange is cheaper (buy there) and which is more expensive (sell there)
        3. Calculate the gross spread percentage between them
        4. Subtract fees for both exchanges (you buy on one, sell on the other — so you pay fees twice)
        5. If net spread > MIN_SPREAD_THRESHOLD → log the opportunity
    */
    
    // when new price comes in, compare it with every *other* exchange that
    // we track.  this allows n>2 exchanges and covers the full Cartesian
    // product of pairs.  (we'll get duplicate comparisons when the opposite
    // side also updates, but that's harmless.)
    for (size_t idx = 0; idx < static_cast<size_t>(Exchange::COUNT); ++idx) {
        Exchange other_exc = static_cast<Exchange>(idx);
        if (other_exc == exc) continue;

        auto other_price = storage_.getPrice(other_exc, sym);
        if (!other_price.has_value()) continue; // no data yet on that exchange

        // always evaluate both directions explicitly; the earlier logic chose
        // one order based on price, but we want to see both buy/sell combinations
        // in the logs so there is no ambiguity.
        auto logComparison = [&](Exchange b_exc, Exchange s_exc,
                                 Price b_price, Price s_price) {
            double spread = ((s_price.toDouble() - b_price.toDouble()) / b_price.toDouble()) * 100.0;
            double net_spread = spread - config::getFee(b_exc) - config::getFee(s_exc);

            std::cout << "COMPARE: " << to_string(sym)
                      << " buy=" << to_string(b_exc) << "($" << b_price.toDouble() << ")"
                      << " sell=" << to_string(s_exc) << "($" << s_price.toDouble() << ")"
                      << " spread=" << spread << "%"
                      << " net=" << net_spread << "%"
                      << std::endl;

            if (net_spread > config::MIN_SPREAD_THRESHOLD) {
                std::cout << "ARBITRAGE: Buy " << to_string(sym) << " on " << to_string(b_exc)
                          << " ($" << b_price.toDouble() << ") → Sell on " << to_string(s_exc)
                          << " ($" << s_price.toDouble() << ") | Net spread: " << net_spread << "%"
                          << std::endl;
            }
        };

        // price from current exchange vs other exchange
        logComparison(exc, other_exc, price, *other_price);
        // and the reverse direction as well
        logComparison(other_exc, exc, *other_price, price);
    }
}
