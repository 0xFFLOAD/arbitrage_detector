#include "fetcher.h"
#include "../shared/logging.h"
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

BitstampFetcher::BitstampFetcher(PriceStorage& storage, Symbol symbol)
    : storage_(storage), symbol_(symbol), running_(false) {
}

BitstampFetcher::~BitstampFetcher() {
    stop();
}

void BitstampFetcher::start() {
    running_ = true;
    thread_ = std::thread(&BitstampFetcher::run, this);
}

void BitstampFetcher::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void BitstampFetcher::run() {
    while (running_) {
        std::string channel = to_bitstamp_channel(symbol_);
        if (channel.empty()) {
            std::cerr << "Bitstamp: no USDC channel for " << to_string(symbol_)
                      << ", retrying in 5s" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        try {
            LOG("Connecting to Bitstamp (" << channel << ")...");

            net::io_context ioc;
            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();

            tcp::resolver resolver(ioc);
            websocket::stream<ssl::stream<beast::tcp_stream>> ws(ioc, ctx);

            auto const results = resolver.resolve("ws.bitstamp.net", "443");
            beast::get_lowest_layer(ws).connect(results);
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "ws.bitstamp.net")) {
                throw beast::system_error(
                    beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                    "Failed to set SNI hostname");
            }
            ws.next_layer().handshake(ssl::stream_base::client);
            std::string host = "ws.bitstamp.net";
            ws.handshake(host, "/");
            beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(5));

            LOG("Connected to Bitstamp!");
            json subscribe = {
                {"event", "bts:subscribe"},
                {"data", {{"channel", channel}}}
            };
            ws.write(net::buffer(subscribe.dump()));

            beast::flat_buffer buffer;
            while (running_) {
                beast::error_code ec;
                ws.read(buffer, ec);
                if (ec == beast::error::timeout) continue;
                if (ec) {
                    ERR("Bitstamp read error: " + ec.message());
                    break;
                }
                std::string message = beast::buffers_to_string(buffer.data());
                json j = json::parse(message);
                if (j.contains("data") && j["data"].contains("price")) {
                    double price;
                    if (j["data"]["price"].is_string())
                        price = std::stod(j["data"]["price"].get<std::string>());
                    else
                        price = j["data"]["price"].get<double>();

                    storage_.updatePrice(Exchange::Bitstamp, symbol_, Price::fromDouble(price));
                    LOG("Bitstamp: " << to_string(symbol_) << " = $" << price);
                }
                buffer.consume(buffer.size());
            }
            beast::error_code ec;
            ws.close(websocket::close_code::normal, ec);
            break;
        } catch (std::exception const& e) {
            if (!running_) break;
            ERR("Bitstamp exception: " << e.what());
        }

        if (running_) {
            ERR("Bitstamp: retrying connection in 5 seconds...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    LOG("Bitstamp Fetcher stopped");
}
