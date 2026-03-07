#pragma once
#include <cstdint>
#include <string>
#include <chrono>
#include <cmath>

enum class Symbol { BTCUSDT, ETHUSDT, BNBUSDT, COUNT };
// NOTE: we previously had BTCUSD, but we only ever compared the same logical
// asset across exchanges.  Each fetcher maps the enum to the appropriate
// exchange-specific ticker (e.g. "BTC-USD" for Coinbase, "BTCUSDT" for
// Binance).  Adding new symbols here automatically expands the storage array.
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
        case Symbol::BTCUSDT: return "BTCUSDT";
        case Symbol::ETHUSDT: return "ETHUSDT";
        case Symbol::BNBUSDT: return "BNBUSDT";
        default: return "UNKNOWN";
    }
}

// helpers for translating to exchange-specific tickers/paths
inline std::string to_binance_stream(Symbol s) {
    std::string stream;
    switch (s) {
        case Symbol::BTCUSDT: stream = "/ws/btcusdt@trade"; break;
        case Symbol::ETHUSDT: stream = "/ws/ethusdt@trade"; break;
        case Symbol::BNBUSDT: stream = "/ws/bnbusdt@trade"; break;
        default: stream = "/";
    }

    if (stream == "/") {
        // try USDC pair for completeness
        std::string sym = to_string(s);
        if (sym.size() > 4 && sym.substr(sym.size() - 4) == "USDT") {
            std::string base = sym.substr(0, sym.size() - 4);
            for (char &c : base) c = std::tolower(c);
            stream = "/ws/" + base + "usdc@trade";
        }
    }

    return stream;
}

inline std::string to_coinbase_product(Symbol s) {
    // explicit mappings for the tokens we care about first
    std::string product;
    switch (s) {
        case Symbol::BTCUSDT: product = "BTC-USD"; break;
        case Symbol::ETHUSDT: product = "ETH-USD"; break;
        // BNB has no native Coinbase feed; leave blank and allow the generic
        // fallback below to try a USDC market before falling back to Coingecko.
        case Symbol::BNBUSDT: product = ""; break;
        default: product = "";
    }

    if (product.empty()) {
        // if we didn't find a mapping, try swapping USDT→USDC automatically
        std::string sym = to_string(s);
        if (sym.size() > 4 && sym.substr(sym.size() - 4) == "USDT") {
            std::string base = sym.substr(0, sym.size() - 4);
            product = base + "-USDC"; // may still be unsupported, caller handles errors
        }
    }

    return product;
}

inline std::string to_bitstamp_channel(Symbol s) {
    std::string channel;
    switch (s) {
        case Symbol::BTCUSDT: channel = "live_trades_btcusd"; break;
        case Symbol::ETHUSDT: channel = "live_trades_ethusd"; break;
        // BNB has no native stream on Bitstamp; we'll let the generic fallback
        // below build a USDC channel and if that also fails the fetcher will
        // drop into the Coingecko path.
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

// map our symbols to CoinGecko identifiers for fallback pricing
inline std::string to_coingecko_id(Symbol s) {
    switch (s) {
        case Symbol::BTCUSDT: return "bitcoin";
        case Symbol::ETHUSDT: return "ethereum";
        case Symbol::BNBUSDT: return "binancecoin";
        default: return "";
    }
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
