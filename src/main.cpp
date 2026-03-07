#include <chrono>
#include <thread>
#include <functional>
#include <iostream>
#include "storage/price_storage.h"
#include "fetcher/fetcher.h"
#include "shared/types.h"
#include "detector/arbitrage_detector.h"

// convert a simple command-line mode into a Symbol; extend as new assets are
// supported
static Symbol modeToSymbol(const std::string &mode) {
    if (mode == "btc") return Symbol::BTCUSDC;
    if (mode == "eth") return Symbol::ETHUSDC;
    if (mode == "bnb") return Symbol::BNBUSDC;
    throw std::runtime_error("unsupported mode: " + mode);
}

int main(int argc, char* argv[]) {
    // determine which symbol/mode to run in; must be provided explicitly
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " [btc|eth]" << std::endl;
        return 1;
    }

    Symbol symbol;
    try {
        symbol = modeToSymbol(argv[1]);
    } catch (std::exception &e) {
        std::cerr << e.what() << "\n";
        std::cerr << "usage: " << argv[0] << " [btc|eth]" << std::endl;
        return 1;
    }

    PriceStorage storage;
    ArbitrageDetector arbitrageDetector(storage);

    storage.subscribe(
        std::bind(&ArbitrageDetector::onPriceUpdate, &arbitrageDetector,
                            std::placeholders::_1,
                            std::placeholders::_2,
                            std::placeholders::_3)
    );

    storage.freeze();
    
    BinanceFetcher binance_fetcher(storage, symbol);
    CoinbaseFetcher coinbase_fetcher(storage, symbol);
    BitstampFetcher bitstamp_fetcher(storage, symbol);
    UniswapFetcher uniswap_fetcher(storage, symbol);
    
    binance_fetcher.start();
    coinbase_fetcher.start();
    bitstamp_fetcher.start();
    uniswap_fetcher.start();
    
    std::this_thread::sleep_for(std::chrono::seconds(60));
    
    binance_fetcher.stop();
    coinbase_fetcher.stop();
    bitstamp_fetcher.stop();  
    uniswap_fetcher.stop();
    
    return 0;
}