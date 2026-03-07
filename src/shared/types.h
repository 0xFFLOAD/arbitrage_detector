#pragma once
#include <cstdint>
#include <string>
#include <chrono>
#include <cmath>

enum class Symbol { BTCUSDC, ETHUSDC, BNBUSDC, COUNT };
// NOTE: the enum names historically referenced USDT because that was the
// most common quote currency, but the detectors now always use USDC.  Each
// fetcher maps the enum to the appropriate exchange-specific ticker (e.g.
// "BTC-USDC" for Coinbase, "BTCUSDC" for Binance).  Adding new symbols
// here automatically expands the storage array.
enum class Exchange { Binance, Coinbase, Bitstamp, Uniswap, COUNT };

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
        case Symbol::BTCUSDC: return "BTCUSDC";
        case Symbol::ETHUSDC: return "ETHUSDC";
        case Symbol::BNBUSDC: return "BNBUSDC";
        default: return "UNKNOWN";
    }
}

// helpers for translating to exchange-specific tickers/paths
inline std::string to_binance_stream(Symbol s) {
    // always subscribe to the USDC-tagged trade stream
    std::string base;
    switch (s) {
        case Symbol::BTCUSDC: base = "btc"; break;
        case Symbol::ETHUSDC: base = "eth"; break;
        case Symbol::BNBUSDC: base = "bnb"; break;
        default: return "/";
    }
    return "/ws/" + base + "usdc@trade";
}

inline std::string to_coinbase_product(Symbol s) {
    // Coinbase provides USD pairs rather than USDC; treat USD as equivalent for
    // our purposes since we don't rely on the quote currency being on-chain.
    switch (s) {
        case Symbol::BTCUSDC: return "BTC-USD";
        case Symbol::ETHUSDC: return "ETH-USD";
        case Symbol::BNBUSDC: return "BNB-USD";
        default: return "";
    }
}

inline std::string to_bitstamp_channel(Symbol s) {
    // prefer USDC channels, but fall back to USD for pairs that only exist
    // in that format (e.g. BNB).  We lowercase the base symbol for the path.
    std::string base;
    switch (s) {
        case Symbol::BTCUSDC: base = "btc"; break;
        case Symbol::ETHUSDC: base = "eth"; break;
        case Symbol::BNBUSDC: base = "bnb"; break;
        default: return "";
    }
    std::string channel = "live_trades_" + base + "usdc";
    // quick HTTP check isn’t appropriate here, so we rely on the websocket
    // later to simply provide no messages if the channel doesn’t exist.  But
    // to improve the odds we also build the USD version so the caller can
    // retry it manually if the first attempt yields nothing.
    if (s == Symbol::BNBUSDC) {
        // BNB/USDC isn’t offered; try BNB/USD instead
        channel = "live_trades_" + base + "usd";
    }
    return channel;
}

inline std::string to_string(Exchange e) {
    switch (e) {
        case Exchange::Binance: return "Binance";
        case Exchange::Coinbase: return "Coinbase";
        case Exchange::Bitstamp: return "Bitstamp";
        case Exchange::Uniswap: return "Uniswap";
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
