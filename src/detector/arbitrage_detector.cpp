#include "arbitrage_detector.h"
#include "../shared/config.h"
#include "../shared/logging.h"
#include <iostream>

ArbitrageDetector::ArbitrageDetector(PriceStorage& storage) : storage_(storage) {
    unsigned int n = std::thread::hardware_concurrency();
    if (n == 0) n = 1;
    for (unsigned int i = 0; i < n; ++i) {
        workers_.emplace_back(&ArbitrageDetector::workerThread, this);
    }
}

ArbitrageDetector::~ArbitrageDetector() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto &t : workers_) {
        if (t.joinable()) t.join();
    }
}

void ArbitrageDetector::onPriceUpdate(Exchange exc, Symbol sym, Price price) {
    // enqueue the update and wake a worker
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({exc, sym, price});
    }
    cv_.notify_one();
}

void ArbitrageDetector::workerThread() {
    while (true) {
        Update upd;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&]{ return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty())
                return;
            upd = queue_.front();
            queue_.pop_front();
        }
        // process the update outside the lock
        Exchange exc = upd.exc;
        Symbol sym = upd.sym;
        Price price = upd.price;

        for (size_t idx = 0; idx < static_cast<size_t>(Exchange::COUNT); ++idx) {
            Exchange other_exc = static_cast<Exchange>(idx);
            if (other_exc == exc) continue;

            auto other_price = storage_.getPrice(other_exc, sym);
            if (!other_price.has_value()) {
                LOG("COMPARE: " << to_string(sym)
                          << " missing price from " << to_string(other_exc));
                continue;
            }

            auto logComparison = [&](Exchange b_exc, Exchange s_exc,
                                     Price b_price, Price s_price) {
                double spread = ((s_price.toDouble() - b_price.toDouble()) / b_price.toDouble()) * 100.0;
                double net_spread = spread - config::getFee(b_exc) - config::getFee(s_exc);

                LOG("COMPARE: " << to_string(sym)
                          << " buy=" << to_string(b_exc) << "($" << b_price.toDouble() << ")"
                          << " sell=" << to_string(s_exc) << "($" << s_price.toDouble() << ")"
                          << " spread=" << spread << "%"
                          << " net=" << net_spread << "%");

                if (net_spread > config::MIN_SPREAD_THRESHOLD) {
                    LOG("ARBITRAGE: Buy " << to_string(sym) << " on " << to_string(b_exc)
                              << " ($" << b_price.toDouble() << ") → Sell on " << to_string(s_exc)
                              << " ($" << s_price.toDouble() << ") | Net spread: " << net_spread << "%");
                }
            };

            logComparison(exc, other_exc, price, *other_price);
            logComparison(other_exc, exc, *other_price, price);
        }
    }
}
