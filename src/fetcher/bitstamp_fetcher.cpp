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

            // if it's BNB we won't even try websocket; the WS channel never
            // produces data despite the REST API listing bnbusd.  Poll instead.
            auto restTicker = [&](Symbol sym) -> double {
                std::string pair;
                switch (sym) {
                    case Symbol::BNBUSDC: pair = "bnbusd"; break;
                    case Symbol::BTCUSDC: pair = "btcusd"; break;
                    case Symbol::ETHUSDC: pair = "ethusd"; break;
                    default: return 0.0;
                }
                try {
                    net::io_context ioc2;
                    ssl::context ctx2(ssl::context::tlsv12_client);
                    ctx2.set_default_verify_paths();
                    tcp::resolver resolver2(ioc2);
                    ssl::stream<beast::tcp_stream> ss(ioc2, ctx2);
                    auto const results2 = resolver2.resolve("www.bitstamp.net", "443");
                    beast::get_lowest_layer(ss).connect(results2);
                    ss.handshake(ssl::stream_base::client);
                    http::request<http::string_body> req{http::verb::get,
                        "/api/v2/ticker/" + pair + "/", 11};
                    req.set(http::field::host, "www.bitstamp.net");
                    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
                    http::write(ss, req);
                    beast::flat_buffer buf2;
                    http::response<http::string_body> res2;
                    http::read(ss, buf2, res2);
                    json r = json::parse(res2.body());
                    if (r.contains("last"))
                        return std::stod(r["last"].get<std::string>());
                } catch (...) {
                }
                return 0.0;
            };

            if (symbol_ == Symbol::BNBUSDC) {
                // simple polling loop
                while (running_) {
                    double p = restTicker(symbol_);
                    if (p > 0.0) {
                        storage_.updatePrice(Exchange::Bitstamp, symbol_, Price::fromDouble(p));
                        LOG("Bitstamp: " << to_string(symbol_) << " = $" << p << " (rest)");
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            } else {
                LOG("Connected to Bitstamp!");
                json subscribe = {
                    {"event", "bts:subscribe"},
                    {"data", {{"channel", channel}}}
                };
                ws.write(net::buffer(subscribe.dump()));

                beast::flat_buffer buffer;
                auto lastUpdate = std::chrono::steady_clock::now();

                while (running_) {
                    beast::error_code ec;
                    ws.read(buffer, ec);
                    if (ec == beast::error::timeout) {
                        if (std::chrono::steady_clock::now() - lastUpdate >
                            std::chrono::seconds(5)) {
                            double p = restTicker(symbol_);
                            if (p > 0.0) {
                                storage_.updatePrice(Exchange::Bitstamp, symbol_, Price::fromDouble(p));
                                LOG("Bitstamp: " << to_string(symbol_) << " = $" << p << " (rest)");
                                lastUpdate = std::chrono::steady_clock::now();
                            }
                        }
                        continue;
                    }
                    if (ec) {
                        ERR("Bitstamp read error: " + ec.message());
                        break;
                    }
                    std::string message = beast::buffers_to_string(buffer.data());
                    json j = json::parse(message);
                    LOG("Bitstamp raw: " << message);
                    if (j.contains("data") && j["data"].contains("price")) {
                        double price;
                        if (j["data"]["price"].is_string())
                            price = std::stod(j["data"]["price"].get<std::string>());
                        else
                            price = j["data"]["price"].get<double>();
                        storage_.updatePrice(Exchange::Bitstamp, symbol_, Price::fromDouble(price));
                        LOG("Bitstamp: " << to_string(symbol_) << " = $" << price);
                        lastUpdate = std::chrono::steady_clock::now();
                    }
                    buffer.consume(buffer.size());
                }
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
