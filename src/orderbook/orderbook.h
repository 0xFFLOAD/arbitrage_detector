#pragma once

#include <optional>
#include <functional>
#include <map>
#include <mutex>
#include <utility>
#include <vector>
#include "../shared/types.h"

class OrderBook {
private:
    mutable std::mutex mutex_;
    std::map<Price, Quantity, std::greater<>> bids_;
    std::map<Price, Quantity> asks_;
public:
    void applySnapshot(const std::vector<std::pair<Price, Quantity>>& bids, const std::vector<std::pair<Price, Quantity>>& asks);

    std::optional<BBO> getBBO() const;

    void update(Side side, Price price, Quantity qty);

    void clear();

    size_t getBidDepth() const;
    size_t getAskDepth() const;
};