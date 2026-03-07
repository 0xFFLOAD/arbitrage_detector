#pragma once 
#include "../storage/price_storage.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>

class ArbitrageDetector {
    public:
        ArbitrageDetector(PriceStorage& storage);
        ~ArbitrageDetector();

        // called by PriceStorage when a new price arrives; this method is
        // lightweight and simply enqueues the work for the background pool.
        void onPriceUpdate(Exchange exc, Symbol sym, Price price);

    private:
        struct Update {
            Exchange exc;
            Symbol sym;
            Price price{0};
        };

        // worker thread function pulls updates from the queue and performs
        // the arbitrage comparison logic.
        void workerThread();

        PriceStorage& storage_;
        std::vector<std::thread> workers_;
        std::deque<Update> queue_;
        std::mutex mutex_;
        std::condition_variable cv_;
        bool stop_ = false;

        // remember which exchange/symbol pairs have ever produced a price;
        // used to suppress "missing price" logs during startup.
        std::array<std::array<bool, static_cast<size_t>(Exchange::COUNT)>,
                   static_cast<size_t>(Symbol::COUNT)> seen_;
};

