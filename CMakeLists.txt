cmake_minimum_required(VERSION 3.15)
project(ArbitrageDetector)

# CMake 3.15+ defaults to CMP0167=NEW (config-mode for FindBoost). Homebrew's
# Boost installation doesn't provide separate package configs for each
# component, which makes the "find_package(Boost COMPONENTS ...)" call fail.
# Force the older module-based behaviour so that FindBoost searches headers
# and libraries directly.
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Require a recent Boost version (Beast is available in 1.66+)
# We avoid specifying COMPONENTS because the Homebrew-provided Boost CMake
# config file lacks separate packages for each component; requesting them
# triggers an error.  We'll link against the needed libraries manually below.
find_package(Boost 1.66 REQUIRED)
find_package(nlohmann_json REQUIRED)

# Help CMake find Homebrew's OpenSSL on macOS
set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)
find_package(OpenSSL REQUIRED)

# Include directories - project headers and Boost's includes
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${Boost_INCLUDE_DIRS})

# Source files
set(SOURCES
    src/main.cpp
    src/storage/price_storage.cpp
    src/fetcher/binance_fetcher.cpp
    src/fetcher/coinbase_fetcher.cpp
    src/detector/arbitrage_detector.cpp
    src/orderbook/orderbook.cpp
)

# Create executable
add_executable(arbitrage ${SOURCES})

# Link libraries: explicitly name boost_system and boost_thread since we
# didn't ask for components above.  ${Boost_LIBRARIES} may be empty, but
# keeping it is harmless for future changes.
target_link_libraries(arbitrage 
    PRIVATE
        ${Boost_LIBRARIES}
        nlohmann_json::nlohmann_json
        OpenSSL::SSL
        OpenSSL::Crypto
)
