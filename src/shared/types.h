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
    // always request a USDC-denominated product
    switch (s) {
        case Symbol::BTCUSDC: return "BTC-USDC";
        case Symbol::ETHUSDC: return "ETH-USDC";
        case Symbol::BNBUSDC: return "BNB-USDC"; // Coinbase supports this market if available
        default: return "";
    }
}

inline std::string to_bitstamp_channel(Symbol s) {
    std::string channel;
    switch (s) {
        case Symbol::BTCUSDT: channel = "live_trades_btcusd"; break;
        case Symbol::ETHUSDT: channel = "live_trades_ethusd"; break;
        // BNB has no native stream on Bitstamp; leave blank so the generic
        // code below will build a USDC channel instead.
        case Symbol::BNBUSDT: channel = ""; break;
        default: channel = "";
    }

    if (channel.empty()) {
        // try a USDC channel by lower‑casing the base symbol
        std::string sym = to_string(s);
        if (sym.size() > 4 && sym.substr(sym.size() - 4) == "USDT") {
            std::string base = sym.substr(0, sym.size() - 4);
            for (char &c : base) c = std::tolower(c);
            channel = "live_trades_" + base + "usdc";
        }
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
