# Arbitrage Detector

A real-time cryptocurrency arbitrage detector written in C++17. Streams live prices from multiple exchanges via WebSocket and detects price discrepancies that could be exploited for profit.

## How It Works

1. **Fetchers** connect to exchange WebSocket APIs and stream live trade prices
2. **PriceStorage** stores the latest prices and notifies subscribers on updates
3. **ArbitrageDetector** compares prices across exchanges and flags opportunities where the spread exceeds trading fees

Currently supported exchanges: **Binance** and **Coinbase**.

## Dependencies

- [Boost.Beast](https://www.boost.org/doc/libs/release/libs/beast/) (WebSocket + HTTP) – requires Boost 1.66 or newer
- [OpenSSL](https://www.openssl.org/) (TLS)
- [nlohmann/json](https://github.com/nlohmann/json) (JSON parsing)
- CMake 3.15+

### macOS (Homebrew)

```sh
brew install boost openssl@3 nlohmann-json cmake
```

## Build

This project uses CMake to locate Boost and other dependencies.  On some
macOS installations (especially with Homebrew) CMake's built-in FindBoost
disappears in favor of Boost's own CMake configuration files; those configs
might not provide separate component packages.  To work around this, the
`CMakeLists.txt` sets policy **CMP0167** to `OLD` and avoids requesting
dependent components explicitly.  You do not need to link against
`boost_system` – the necessary pieces are header-only in recent Boost
releases.

```sh
mkdir -p build && cd build
cmake ..
make
```

## Run

You must specify which asset the program should monitor by passing a mode
argument.  Supported values are `btc` and `eth`; there is **no default**.
Additionally, three exchanges are queried (Binance, Coinbase, Bitstamp) and
the program aggregates prices from all of them.  The code is structured so
adding another symbol or exchange only requires updating the appropriate enum
and helper/fetcher class.

```sh
# Bitcoin mode
./arbitrage btc

# Ethereum mode
./arbitrage eth
```

By default the detector only logs *opportunities* where the net spread after
fees exceeds the `MIN_SPREAD_THRESHOLD` (0.1 %).  Because Binance and Coinbase
prices are typically within a few hundredths of a percent, you may only see
price updates rather than arbitrage messages.  To observe the comparison logic
and debug the calculation, the program prints a `COMPARE:` line for each
pair of prices; you can also lower `MIN_SPREAD_THRESHOLD` in
`src/shared/config.h` (or set it to `0`) to force arbitrage output.

The fetchers are now resilient: if the websocket connection drops or an
exception occurs, they log an error and automatically retry after a short
(5 s) backoff.  They continue attempting to reconnect until the main thread
calls `stop()` (currently after 60 s).  This makes short network blips or rate
limit disconnects invisible to the rest of the program; you only see the
"Binance: retrying connection" or similar messages in the console.

Since there are now three exchanges, the detector compares every pair of
venues each time a new price arrives.  That means you will see `COMPARE:`
lines for Binance–Coinbase, Binance–Bitstamp and Coinbase–Bitstamp whenever
all three have data.  Arbitrage messages are still only printed when the net
spread after fees exceeds the threshold.

> **Editor helpers** – a `compile_commands.json` file is generated in `build`
and symlinked at the workspace root.  A `.vscode` folder with
`c_cpp_properties.json` and `settings.json` is included so VS Code/clangd and
the Microsoft C++ extension automatically pick up the include paths
(including `/opt/homebrew/include` for Boost).  Reload the window or restart
the language server if you see any "cannot open source file <boost/...>"
errors; the configuration is already present.

## License

MIT
