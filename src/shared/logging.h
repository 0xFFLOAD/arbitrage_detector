#pragma once

#include <mutex>
#include <iostream>

// simple thread-safe logging macros using a global mutex
inline std::mutex& getLogMutex() {
    static std::mutex m;
    return m;
}

#define LOG(x) \
    do { \
        std::lock_guard<std::mutex> lock(getLogMutex()); \
        std::cout << x << std::endl; \
    } while (0)

#define ERR(x) \
    do { \
        std::lock_guard<std::mutex> lock(getLogMutex()); \
        std::cerr << x << std::endl; \
    } while (0)
