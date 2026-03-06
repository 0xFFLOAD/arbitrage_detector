cmake_minimum_required(VERSION 3.15)
project(ArbitrageDetector)

# generate compile_commands.json for IDEs like clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# Require a recent Boost version (Beast is available in 1.66+).
# Use the module-based search; we don't request specific components because
# the Homebrew-supplied BoostConfig doesn't provide separate component packages.
# We rely on ${Boost_INCLUDE_DIRS} to get the headers.
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

find_package(Boost 1.66 REQUIRED)
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
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
    src/fetcher/bitstamp_fetcher.cpp
    src/detector/arbitrage_detector.cpp
    src/orderbook/orderbook.cpp
)

# Create executable
add_executable(arbitrage ${SOURCES})

# Ensure the target sees Boost include directories so the IDE can resolve
# <boost/...> headers even though the libraries are header-only.
target_include_directories(arbitrage PRIVATE ${Boost_INCLUDE_DIRS})

# Link libraries: ${Boost_LIBRARIES} will contain -lboost_system etc. when
# necessary.  We also explicitly add OpenSSL and JSON.
target_link_libraries(arbitrage 
    PRIVATE
        ${Boost_LIBRARIES}
        nlohmann_json::nlohmann_json
        OpenSSL::SSL
        OpenSSL::Crypto
)
