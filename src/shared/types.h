#pragma once
#include <cstdint>
#include <string>
#include <chrono>
#include <cmath>

enum class Symbol { BTCUSDT, ETHUSDT, COUNT };
// NOTE: we previously had BTCUSD, but we only ever compared the same logical
// asset across exchanges.  Each fetcher maps the enum to the appropriate
// exchange-specific ticker (e.g. "BTC-USD" for Coinbase, "BTCUSDT" for
// Binance).  Adding new symbols here automatically expands the storage array.
enum class Exchange { Binance, Coinbase, Bitstamp, COUNT };

enum class Side { Bid, Ask };

constexpr size_t getPriceIndex(Exchange exc, Symbol sym) {
    return static_cast<size_t>(exc) * static_cast<size_t>(Symbol::COUNT) + static_cast<size_t>(sym);
}

class Quantity {
    int64_t value = 0;
public:
    static constexpr int64_t SCALE = 100000000;

    Quantity() = default;

    explicit Quantity(int64_t v) : value(v) {};

    static Quantity fromDouble(double v) {
        return Quantity{std::llround(v * SCALE)};
    }

    double toDouble() const {
        return static_cast<double>(value) / SCALE;
    }

    bool operator==(const Quantity& other) const {
        return other.value == value;
    }
};

class Price {
    int64_t value;

    public:
        static constexpr int64_t SCALE = 100000;

        int64_t getValue() const {
            return value;
        }

        explicit Price(int64_t v) : value(v) {};

        static Price fromDouble(double d) {
            return Price { std::llround(d * SCALE)};
        }

        double toDouble() const {
            return static_cast<double>(this->value) / Price::SCALE;
        }

        bool operator<(const Price& otherPrice) const {
            return this->value < otherPrice.value;
        }

        bool operator>(const Price& otherPrice) const {
            return this->value > otherPrice.value;
        }

        bool operator==(const Price& otherPrice) const {
            return this->value == otherPrice.value;
        }

        Price operator-(const Price& otherPrice) const {
            return Price { this->value - otherPrice.value};
        }
};

struct BBO {
    Price bestBid;
    Quantity bidQty;
    Price bestAsk;
    Quantity askQty;
};

inline std::string to_string(Symbol s) {
    switch (s) {
        case Symbol::BTCUSDT: return "BTCUSDT";
        case Symbol::ETHUSDT: return "ETHUSDT";
        default: return "UNKNOWN";
    }
}

// helpers for translating to exchange-specific tickers/paths
inline std::string to_binance_stream(Symbol s) {
    switch (s) {
        case Symbol::BTCUSDT: return "/ws/btcusdt@trade";
        case Symbol::ETHUSDT: return "/ws/ethusdt@trade";
        default: return "/";
    }
}

inline std::string to_coinbase_product(Symbol s) {
    switch (s) {
        case Symbol::BTCUSDT: return "BTC-USD";
        case Symbol::ETHUSDT: return "ETH-USD";
        default: return "";
    }
}

inline std::string to_bitstamp_channel(Symbol s) {
    switch (s) {
        case Symbol::BTCUSDT: return "live_trades_btcusd";
        case Symbol::ETHUSDT: return "live_trades_ethusd";
        default: return "";
    }
}
inline std::string to_string(Exchange e) {
    switch (e) {
        case Exchange::Binance: return "Binance";
        case Exchange::Coinbase: return "Coinbase";
        case Exchange::Bitstamp: return "Bitstamp";
        default: return "UNKNOWN";
    }
}

// represents a detected arbitrage opportunity
struct ArbitrageOpportunity {
    Exchange buy_exchange;          // where to buy (lower price)
    Exchange sell_exchange;         // where to sell (higher price)
    Symbol symbol;
    double buy_price;
    double sell_price;
    double spread_percent;          // gross spread before fees
    double net_spread_percent;      // spread after fees (actual profit)
    std::chrono::system_clock::time_point timestamp;
};
