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
            // WBTC / USDT v2 pool
            return "0x0d4a11d5eEaaC28EC3F61d100b30fB5Ba42BbF5";
        case Symbol::ETHUSDT:
            // WETH / USDT v2 pool
            return "0x8ad599c3a0ff1de082011efddc58f1908eb6e6d8";
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
    // we'll query a public Ethereum JSON-RPC endpoint (Cloudflare) periodically
    const std::string rpcHost = "cloudflare-eth.com";
    const std::string rpcPort = "443";

    while (running_) {
        try {
            net::io_context ioc;
            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();

            tcp::resolver resolver(ioc);
            ssl::stream<beast::tcp_stream> stream(ioc, ctx);

            auto const results = resolver.resolve(rpcHost, rpcPort);
            beast::get_lowest_layer(stream).connect(results);
            if (!SSL_set_tlsext_host_name(stream.native_handle(), rpcHost.c_str())) {
                throw beast::system_error(
                    beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                    "Failed to set SNI hostname");
            }
            stream.handshake(ssl::stream_base::client);

            // build JSON-RPC request for getReserves
            json body = {
                {"jsonrpc", "2.0"},
                {"id", 1},
                {"method", "eth_call"},
                {"params", json::array({
                    {
                        {"to", getPoolAddress(symbol_)},
                        {"data", "0x0902f1ac"} // getReserves()
                    },
                    "latest"
                })}
            };

            http::request<http::string_body> req{http::verb::post, "/", 11};
            req.set(http::field::host, rpcHost);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.set(http::field::content_type, "application/json");
            req.body() = body.dump();
            req.prepare_payload();

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            // parse response
            json resp = json::parse(res.body());
            if (resp.contains("result")) {
                std::string hex = resp["result"].get<std::string>();
                if (hex.rfind("0x", 0) == 0 && hex.size() >= 2 + 64 * 2) {
                    std::string r0hex = hex.substr(2, 64);
                    std::string r1hex = hex.substr(2 + 64, 64);

                    // convert hex strings to integers
                    boost::multiprecision::cpp_int r0(0);
                    boost::multiprecision::cpp_int r1(0);
                    r0 = 0;
                    r1 = 0;
                    for (char c : r0hex) {
                        r0 <<= 4;
                        if (c >= '0' && c <= '9') r0 += c - '0';
                        else if (c >= 'a' && c <= 'f') r0 += 10 + (c - 'a');
                        else if (c >= 'A' && c <= 'F') r0 += 10 + (c - 'A');
                    }
                    for (char c : r1hex) {
                        r1 <<= 4;
                        if (c >= '0' && c <= '9') r1 += c - '0';
                        else if (c >= 'a' && c <= 'f') r1 += 10 + (c - 'a');
                        else if (c >= 'A' && c <= 'F') r1 += 10 + (c - 'A');
                    }

                    double dec0 = getToken0Decimals(symbol_);
                    double dec1 = getToken1Decimals(symbol_);
                    // price of token0 in terms of token1
                    double ratio = r1.convert_to<double>() / r0.convert_to<double>();
                    double price = ratio * std::pow(10.0, dec0 - dec1);
                    Price p = Price::fromDouble(price);
                    storage_.updatePrice(Exchange::Uniswap, symbol_, p);
                    std::cout << "Uniswap: " << to_string(symbol_) << " = $" << price << std::endl;
                }
            }

            // close connection gracefully
            beast::error_code ec;
            stream.shutdown(ec);
            // ignore shutdown error
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
