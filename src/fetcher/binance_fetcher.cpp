#include "fetcher.h"
#include "../shared/logging.h"
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;


BinanceFetcher::BinanceFetcher(PriceStorage& storage, Symbol symbol)
    : storage_(storage), symbol_(symbol), running_(false) {
}

BinanceFetcher::~BinanceFetcher() {
    BinanceFetcher::stop();
}

void BinanceFetcher::start() {
    running_ = true;
    thread_ = std::thread(&BinanceFetcher::run, this);
}

void BinanceFetcher::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void BinanceFetcher::run() {
    while (running_) {
        try {
            LOG("Connecting to Binance...");
            
            net::io_context ioc;
            ssl::context ctx(ssl::context::tlsv12_client);
            
            ctx.set_default_verify_paths();
            
            tcp::resolver resolver(ioc);
            websocket::stream<ssl::stream<beast::tcp_stream>> ws(ioc, ctx);
            
            auto const results = resolver.resolve("stream.binance.com", "9443");
            
            // setp 1: connect tcp
            beast::get_lowest_layer(ws).connect(results);

            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "stream.binance.com")) {
                throw beast::system_error(
                    beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                    "Failed to set SNI hostname");
            }
            
            // step 2: ssl handshake 
            ws.next_layer().handshake(ssl::stream_base::client);
            
            std::string host = "stream.binance.com:9443";
            
            // step 3: websocket handshake 
            ws.handshake(host, to_binance_stream(symbol_));

            beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(5));

            LOG("Connected to Binance!");
            
            beast::flat_buffer buffer;
            
            while (running_) {
                beast::error_code ec;
                ws.read(buffer, ec);

                if (ec == beast::error::timeout) continue;

                if (ec) {
                    ERR("Binance read error: " + ec.message());
                    break;
                }
                
                std::string message = beast::buffers_to_string(buffer.data());
                json j = json::parse(message);
                
                if (j.contains("p")) {
                    double price = std::stod(j["p"].get<std::string>());
                    Price int_price = Price::fromDouble(price);
                    storage_.updatePrice(Exchange::Binance, symbol_, int_price);
                    LOG("Binance: " << to_string(symbol_) << " = $" << price);
                }
                
                buffer.consume(buffer.size());
            }
            
            beast::error_code ec;
            ws.close(websocket::close_code::normal, ec);
            // ignore close errors

            break; // normal exit
        } catch (boost::system::system_error const& e) {
            if (!running_) break;
            if (e.code() != net::ssl::error::stream_truncated) {
                ERR("Binance connection error: " << e.what());
            }
        } catch (std::exception const& e) {
            if (!running_) break;
            ERR("Binance exception: " << e.what());
        }

        if (running_) {
            ERR("Binance: retrying connection in 5 seconds...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    LOG("Binance Fetcher stopped");
}