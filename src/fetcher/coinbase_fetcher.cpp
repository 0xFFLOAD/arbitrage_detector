#include "fetcher.h"
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
    // determine whether to use websocket or fallback to Coingecko
    std::string product = to_coinbase_product(symbol_);
    std::string cg_id = to_coingecko_id(symbol_);

    if (product.empty() && !cg_id.empty()) {
        // no direct feed available, poll coingecko instead
        while (running_) {
            try {
                net::io_context ioc;
                ssl::context ctx(ssl::context::tlsv12_client);
                ctx.set_default_verify_paths();

                tcp::resolver resolver(ioc);
                ssl::stream<beast::tcp_stream> stream(ioc, ctx);
                auto const results = resolver.resolve("api.coingecko.com", "443");
                beast::get_lowest_layer(stream).connect(results);
                if (!SSL_set_tlsext_host_name(stream.native_handle(), "api.coingecko.com"))
                    throw beast::system_error(
                        beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                        "Failed to set SNI hostname");
                stream.handshake(ssl::stream_base::client);

                std::string path = "/api/v3/simple/price?ids=" + cg_id + "&vs_currencies=usd";
                http::request<http::string_body> req{http::verb::get, path, 11};
                req.set(http::field::host, "api.coingecko.com");
                req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
                http::write(stream, req);

                beast::flat_buffer buffer;
                http::response<http::string_body> res;
                http::read(stream, buffer, res);
                json j = json::parse(res.body());
                if (j.contains(cg_id) && j[cg_id].contains("usd")) {
                    double price = j[cg_id]["usd"].get<double>();
                    storage_.updatePrice(Exchange::Coinbase, symbol_, Price::fromDouble(price));
                    std::cout << "Coinbase (fallback): " << to_string(symbol_) << " = $" << price << std::endl;
                }

                beast::error_code ec;
                stream.shutdown(ec);
            } catch (std::exception const& e) {
                if (!running_) break;
                std::cerr << "Coinbase fallback exception: " << e.what() << std::endl;
            }
            if (running_) std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        std::cout << "Coinbase Fetcher stopped" << std::endl;
        return;
    }

    // otherwise use websocket feed
    while (running_) {
        try {
            std::cout << "Connecting to Coinbase..." << std::endl;
            
            net::io_context ioc;
            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();
            
            tcp::resolver resolver(ioc);
            websocket::stream < ssl::stream <beast::tcp_stream>> ws(ioc, ctx);

            // resolve host
            auto const results = resolver.resolve("ws-feed.exchange.coinbase.com", "443");
            
            // TCP connect
            beast::get_lowest_layer(ws).connect(results);

            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), 
                                        "ws-feed.exchange.coinbase.com")) {
                throw beast::system_error(
                    beast::error_code(static_cast<int>(::ERR_get_error()), 
                                    net::error::get_ssl_category()),
                    "Failed to set SNI hostname");
            }
            
            // SSL handshake
            ws.next_layer().handshake(ssl::stream_base::client);
            
            // WS handshake
            std::string host = "ws-feed.exchange.coinbase.com";
            ws.handshake(host, "/");

            beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(5));

            std::cout << "Connected to Coinbase!" << std::endl;
            
            // send subscription message (key difference between binance and coinbase)
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
                    std::cerr << "Coinbase read error: " + ec.message() << std::endl;
                    break;
                }
                
                std::string message = beast::buffers_to_string(buffer.data());
                json j = json::parse(message);
                
                if (j.contains("type") && j["type"] == "match" && j.contains("price")) {
                    double price = std::stod(j["price"].get<std::string>());
                    Price int_price = Price::fromDouble(price);
                    storage_.updatePrice(Exchange::Coinbase, symbol_, int_price);
                    std::cout << "Coinbase: " << to_string(symbol_) << " = $" << price << std::endl;
                }
                
                buffer.consume(buffer.size());
            }
            
            beast::error_code ec;
            ws.close(websocket::close_code::normal, ec);
            // ignore errors on close

            break; // normal exit
        } catch (std::exception const& e) {
            if (!running_) break;
            std::cerr << "Coinbase exception: " << e.what() << std::endl;
        }

        if (running_) {
            std::cerr << "Coinbase: retrying connection in 5 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    std::cout << "Coinbase Fetcher stopped" << std::endl;
}