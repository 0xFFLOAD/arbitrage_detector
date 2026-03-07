#include "fetcher.h"
#include "../shared/logging.h"
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

CoinbaseFetcher::CoinbaseFetcher(PriceStorage& storage, Symbol symbol)  
    : storage_(storage), symbol_(symbol), running_(false) {
    }

CoinbaseFetcher::~CoinbaseFetcher() {
    stop();
}

void CoinbaseFetcher::start() {
    running_ = true;
    thread_ = std::thread(&CoinbaseFetcher::run, this);
}

void CoinbaseFetcher::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void CoinbaseFetcher::run() {
    while (running_) {
        // try to build a websocket product; helper now always returns a USDC product
        std::string product = to_coinbase_product(symbol_);
        if (product.empty()) {
            std::cerr << "Coinbase: no USD market for " << to_string(symbol_)
                      << ", sleeping before retry" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        try {
            LOG("Connecting to Coinbase (" << product << ")...");
            net::io_context ioc;
            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();
            tcp::resolver resolver(ioc);
            websocket::stream<ssl::stream<beast::tcp_stream>> ws(ioc, ctx);

            auto const results = resolver.resolve("ws-feed.exchange.coinbase.com", "443");
            beast::get_lowest_layer(ws).connect(results);
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(),
                                         "ws-feed.exchange.coinbase.com")) {
                throw beast::system_error(
                    beast::error_code(static_cast<int>(::ERR_get_error()),
                                      net::error::get_ssl_category()),
                    "Failed to set SNI hostname");
            }
            ws.next_layer().handshake(ssl::stream_base::client);
            std::string host = "ws-feed.exchange.coinbase.com";
            ws.handshake(host, "/");
            beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(5));

            LOG("Connected to Coinbase!");
            json subscribe_msg = {
                {"type", "subscribe"},
                {"product_ids", {product}},
                {"channels", {"matches"}}
            };
            ws.write(net::buffer(subscribe_msg.dump()));

            beast::flat_buffer buffer;
            while (running_) {
                beast::error_code ec;
                ws.read(buffer, ec);
                if (ec == beast::error::timeout) continue;
                if (ec) {
                    ERR("Coinbase read error: " + ec.message());
                    break;
                }
                std::string message = beast::buffers_to_string(buffer.data());
                json j = json::parse(message);
                if (j.contains("type") && j["type"] == "match" && j.contains("price")) {
                    double price = std::stod(j["price"].get<std::string>());
                    storage_.updatePrice(Exchange::Coinbase, symbol_, Price::fromDouble(price));
                    LOG("Coinbase: " << to_string(symbol_) << " = $" << price);
                }
                buffer.consume(buffer.size());
            }
            beast::error_code ec;
            ws.close(websocket::close_code::normal, ec);
            break;
        } catch (std::exception const& e) {
            if (!running_) break;
            ERR("Coinbase exception: " << e.what());
        }

        if (running_) {
            ERR("Coinbase: retrying connection in 5 seconds...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    LOG("Coinbase Fetcher stopped");
}