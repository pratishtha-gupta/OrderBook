#pragma once

#include "price_level.hpp"
#include "trade.hpp"

#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

class OrderBook {
public:
    OrderBook() = default;

    // Copying would require rebuilding every stored list iterator.
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = default;
    OrderBook& operator=(OrderBook&&) = default;

    std::vector<Trade> submit_limit_order(
        OrderId order_id,
        Side side,
        Price price,
        Quantity quantity,
        TimeInForce time_in_force
    );

    // Market orders consume available liquidity and never rest on the book.
    std::vector<Trade> submit_market_order(
        OrderId order_id,
        Side side,
        Quantity quantity
    );

    [[nodiscard]] bool cancel_order(OrderId order_id);

    std::vector<Trade> amend_order(
        OrderId order_id,
        Price new_price,
        Quantity new_quantity
    );

    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;

private:
    struct OrderLocation {
        Side side;
        Price price;
        PriceLevel::Iterator order;
    };

    // Price maps select the best level; each PriceLevel provides FIFO priority.
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel> asks_;

    // Active order IDs point directly to their list node for cancel/amend.
    std::unordered_map<OrderId, OrderLocation> order_locations_;

    [[nodiscard]] bool can_fully_fill(
        Side side,
        Price limit_price,
        Quantity quantity
    ) const;

    std::vector<Trade> match_order(
        OrderId order_id,
        Side side,
        Quantity& remaining,
        std::optional<Price> limit_price
    );

    void rest_order(
        OrderId order_id,
        Side side,
        Price price,
        Quantity quantity
    );
};
