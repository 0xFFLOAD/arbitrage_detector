#include "fetcher.h"
#include <iostream>
#include <nlohmann/json.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <cmath>

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;


UniswapFetcher::UniswapFetcher(PriceStorage &storage, Symbol symbol)
    : storage_(storage), symbol_(symbol), running_(false) {
}

UniswapFetcher::~UniswapFetcher() {
    stop();
}

void UniswapFetcher::start() {
    running_ = true;
    thread_ = std::thread(&UniswapFetcher::run, this);
}

void UniswapFetcher::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

// helper functions for pool configuration
std::string UniswapFetcher::getPoolAddress(Symbol sym) {
    switch (sym) {
        case Symbol::BTCUSDT:
            // TODO: verify this address points to WBTC/USDT; the factory call
            // below revealed that 0x0d4a11...5Ba42BbF5 actually corresponds to
            // WBTC/WETH, so the BTCUSDT pair may still need correction.
            return "0x0d4a11d5eEaaC28EC3F61d100b30fB5Ba42BbF5";
        case Symbol::ETHUSDT:
            // WETH / USDT v2 pool (obtained via Uniswap factory getPair)
            return "0x0d4a11d5eeaac28ec3f61d100daf4d40471f1852";
        default:
            return "";
    }
}

int UniswapFetcher::getToken0Decimals(Symbol sym) {
    switch (sym) {
        case Symbol::BTCUSDT: return 8;   // WBTC has 8 decimals
        case Symbol::ETHUSDT: return 18;  // WETH has 18 decimals
        default: return 0;
    }
}

int UniswapFetcher::getToken1Decimals(Symbol sym) {
    // USDT has 6 decimals for both pairs
    return 6;
}

void UniswapFetcher::run() {
    // Instead of calling an Ethereum node directly (which frequently returned
    // "internal error" or execution reverted), we fetch prices from Coingecko's
    // Uniswap V2 exchange tickers endpoint.  This avoids reliance on RPC
    // providers that may throttle or block the requests.
    const std::string apiHost = "api.coingecko.com";
    const std::string apiPort = "443";
    const std::string apiPath = "/api/v3/exchanges/uniswap_v2/tickers";

    // token addresses in lowercase for comparison
    const std::string weth = "0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2";
    const std::string wbtc = "0x2260fac5e5542a773aa44fbcfedf7c193bc2c599";
    const std::string wbnb = "0xbb4cdb9cbd36b01bd1cbaebf2de08d9173bc095c"; // Wrapped BNB on Ethereum
    const std::string usdt = "0xdac17f958d2ee523a2206206994597c13d831ec7";

    while (running_) {
        try {
            net::io_context ioc;
            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();

            tcp::resolver resolver(ioc);
            ssl::stream<beast::tcp_stream> stream(ioc, ctx);

            auto const results = resolver.resolve(apiHost, apiPort);
            beast::get_lowest_layer(stream).connect(results);
            if (!SSL_set_tlsext_host_name(stream.native_handle(), apiHost.c_str())) {
                throw beast::system_error(
                    beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                    "Failed to set SNI hostname");
            }
            stream.handshake(ssl::stream_base::client);

            http::request<http::string_body> req{http::verb::get, apiPath, 11};
            req.set(http::field::host, apiHost);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            json resp = json::parse(res.body());
            if (resp.contains("tickers")) {
                for (auto &t : resp["tickers"]) {
                    // addresses sometimes uppercase; normalize to lowercase
                    std::string base = t["base"].get<std::string>();
                    std::string target = t["target"].get<std::string>();
                    std::transform(base.begin(), base.end(), base.begin(), ::tolower);
                    std::transform(target.begin(), target.end(), target.begin(), ::tolower);

                    bool match = false;
                    double price = 0.0;
                    if (symbol_ == Symbol::ETHUSDT) {
                        if ((base == weth && target == usdt) ||
                            (base == usdt && target == weth)) {
                            match = true;
                        }
                    } else if (symbol_ == Symbol::BTCUSDT) {
                        if ((base == wbtc && target == usdt) ||
                            (base == usdt && target == wbtc)) {
                            match = true;
                        }
                    } else if (symbol_ == Symbol::BNBUSDT) {
                        if ((base == wbnb && target == usdt) ||
                            (base == usdt && target == wbnb)) {
                            match = true;
                        }
                    }

                    if (match) {
                        double last = t["last"].get<double>();
                        if ((symbol_ == Symbol::ETHUSDT && base == weth) ||
                            (symbol_ == Symbol::BTCUSDT && base == wbtc)) {
                            price = last;
                        } else {
                            // base is USDT, invert
                            if (last != 0.0) price = 1.0 / last;
                        }
                        // fallback to converted USD if zero or weird
                        if (price == 0.0 && t["converted_last"].is_object()) {
                            price = t["converted_last"]["usd"].get<double>();
                        }
                        if (price > 0.0) {
                            Price p = Price::fromDouble(price);
                            storage_.updatePrice(Exchange::Uniswap, symbol_, p);
                            std::cout << "Uniswap: " << to_string(symbol_) << " = $" << price << std::endl;
                            break; // use first matching ticker
                        }
                    }
                }
            }

            beast::error_code ec;
            stream.shutdown(ec);
        } catch (std::exception const &e) {
            if (!running_) break;
            std::cerr << "Uniswap exception: " << e.what() << std::endl;
        }

        if (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    std::cout << "Uniswap Fetcher stopped" << std::endl;
}
