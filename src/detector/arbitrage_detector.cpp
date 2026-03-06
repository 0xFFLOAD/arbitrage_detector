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

        Price buy_price{0}, sell_price{0};
        Exchange buy_exc, sell_exc;

        if (price < *other_price) {
            buy_exc = exc;
            sell_exc = other_exc;
            buy_price = price;
            sell_price = *other_price;
        } else {
            buy_exc = other_exc;
            sell_exc = exc;
            buy_price = *other_price;
            sell_price = price;
        }

        double spread = ((sell_price.toDouble() - buy_price.toDouble()) / buy_price.toDouble()) * 100.0;
        double net_spread = spread - config::getFee(buy_exc) - config::getFee(sell_exc);

        // debug output so we can see all comparisons
        std::cout << "COMPARE: " << to_string(sym)
                  << " buy=" << to_string(buy_exc) << "($" << buy_price.toDouble() << ")"
                  << " sell=" << to_string(sell_exc) << "($" << sell_price.toDouble() << ")"
                  << " spread=" << spread << "%"
                  << " net=" << net_spread << "%"
                  << std::endl;

        if (net_spread > config::MIN_SPREAD_THRESHOLD) {
            std::cout << "ARBITRAGE: Buy " << to_string(sym) << " on " << to_string(buy_exc)
                      << " ($" << buy_price.toDouble() << ") → Sell on " << to_string(sell_exc)
                      << " ($" << sell_price.toDouble() << ") | Net spread: " << net_spread << "%"
                      << std::endl;
        }
    }
}
