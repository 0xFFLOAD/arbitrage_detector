#include "orderbook.h"
#include <cstddef>
#include <mutex>
#include <optional>

void OrderBook::applySnapshot(const std::vector<std::pair<Price, Quantity>>& bids, const std::vector<std::pair<Price, Quantity>>& asks) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    bids_.clear();
    asks_.clear();

    for (const auto& [price, qty]: bids) {
        bids_[price] = qty;
    }

    for (const auto& [price, qty]: asks) {
        asks_[price] = qty;
    }
}

void OrderBook::update(Side side, Price price, Quantity qty) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (qty == Quantity{0}) {
        // remove from orderbook
        if (side == Side::Ask) {
            asks_.erase(price);
        } else {
            bids_.erase(price);
        }
        return;
    }

    // update or insert
    if (side == Side::Ask) {
        asks_[price] = qty;
    } else {
        bids_[price] = qty;
    }
}

std::optional<BBO> OrderBook::getBBO() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (bids_.empty() || asks_.empty())
        return std::nullopt;

    BBO bbo{
        bids_.begin()->first, 
        bids_.begin()->second,
        asks_.begin()->first, 
        asks_.begin()->second
    };

    return bbo;
}

void OrderBook::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    bids_.clear();
    asks_.clear();
}

size_t OrderBook::getAskDepth() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return asks_.size();
}

size_t OrderBook::getBidDepth() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return bids_.size();
}