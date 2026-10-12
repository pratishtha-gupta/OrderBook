#include "orderbook.hpp"

#include <algorithm>
#include <stdexcept>

namespace {

void validate_side(Side side)
{
    if (side != Side::buy && side != Side::sell) {
        throw std::invalid_argument("Invalid order side.");
    }
}

void validate_quantity(Quantity quantity)
{
    if (quantity == 0) {
        throw std::invalid_argument("Quantity cannot be zero.");
    }
}

void validate_price(Price price)
{
    if (price == 0) {
        throw std::invalid_argument("Price cannot be zero.");
    }
}

void validate_time_in_force(TimeInForce time_in_force)
{
    if (time_in_force != TimeInForce::gtc &&
        time_in_force != TimeInForce::ioc &&
        time_in_force != TimeInForce::fok) {
        throw std::invalid_argument("Invalid time in force.");
    }
}

} // namespace

std::vector<Trade> OrderBook::submit_limit_order(
    OrderId order_id,
    Side side,
    Price price,
    Quantity quantity,
    TimeInForce time_in_force)
{
    if (order_locations_.contains(order_id)) {
        throw std::invalid_argument("Order ID already exists.");
    }

    validate_side(side);
    validate_price(price);
    validate_quantity(quantity);
    validate_time_in_force(time_in_force);

    // FOK must prove sufficient crossed liquidity before changing the book.
    if (time_in_force == TimeInForce::fok &&
        !can_fully_fill(side, price, quantity)) {
        return {};
    }

    Quantity remaining = quantity;
    auto trades = match_order(order_id, side, remaining, price);

    if (remaining > 0 && time_in_force == TimeInForce::gtc) {
        rest_order(order_id, side, price, remaining);
    }

    return trades;
}

std::vector<Trade> OrderBook::submit_market_order(
    OrderId order_id,
    Side side,
    Quantity quantity)
{
    if (order_locations_.contains(order_id)) {
        throw std::invalid_argument("Order ID already exists.");
    }

    validate_side(side);
    validate_quantity(quantity);

    Quantity remaining = quantity;
    return match_order(order_id, side, remaining, std::nullopt);
}

bool OrderBook::can_fully_fill(
    Side side,
    Price limit_price,
    Quantity quantity) const
{
    const auto has_quantity = [quantity](const auto& levels,
                                         const auto& crosses) {
        Quantity remaining = quantity;

        for (const auto& [level_price, level] : levels) {
            if (!crosses(level_price)) {
                break;
            }
            if (level.total_quantity() >= remaining) {
                return true;
            }
            remaining -= level.total_quantity();
        }
        return false;
    };

    if (side == Side::buy) {
        return has_quantity(asks_, [limit_price](Price ask) {
            return ask <= limit_price;
        });
    }

    return has_quantity(bids_, [limit_price](Price bid) {
        return bid >= limit_price;
    });
}

std::vector<Trade> OrderBook::match_order(
    OrderId order_id,
    Side side,
    Quantity& remaining,
    std::optional<Price> limit_price)
{
    std::vector<Trade> trades;

    const auto match_levels = [&](auto& levels, const auto& crosses) {
        while (remaining > 0 && !levels.empty()) {
            auto level_it = levels.begin();
            const Price execution_price = level_it->first;

            if (!crosses(execution_price)) {
                break;
            }

            PriceLevel& level = level_it->second;
            const Order& maker = level.front_order();
            const OrderId maker_order_id = maker.order_id;
            const Quantity maker_quantity = maker.quantity;
            const Quantity executed = std::min(remaining, maker_quantity);

            trades.push_back(Trade{
                maker_order_id,
                order_id,
                execution_price,
                executed
            });

            level.apply_fill_front(executed);
            remaining -= executed;

            if (executed == maker_quantity) {
                order_locations_.erase(maker_order_id);
            }
            if (level.empty()) {
                levels.erase(level_it);
            }
        }
    };

    if (side == Side::buy) {
        match_levels(asks_, [limit_price](Price ask) {
            return !limit_price || ask <= *limit_price;
        });
    } else {
        match_levels(bids_, [limit_price](Price bid) {
            return !limit_price || bid >= *limit_price;
        });
    }

    return trades;
}

bool OrderBook::cancel_order(OrderId order_id)
{
    const auto location_it = order_locations_.find(order_id);
    if (location_it == order_locations_.end()) {
        return false;
    }

    const OrderLocation location = location_it->second;
    const auto erase_from_level = [&location](auto& levels) {
        const auto level_it = levels.find(location.price);
        if (level_it == levels.end()) {
            throw std::logic_error("Order location refers to a missing price level.");
        }

        PriceLevel& level = level_it->second;
        level.erase_order(location.order);
        if (level.empty()) {
            levels.erase(level_it);
        }
    };

    if (location.side == Side::buy) {
        erase_from_level(bids_);
    } else if (location.side == Side::sell) {
        erase_from_level(asks_);
    } else {
        throw std::logic_error("Order location contains an invalid side.");
    }

    order_locations_.erase(location_it);
    return true;
}

std::vector<Trade> OrderBook::amend_order(
    OrderId order_id,
    Price new_price,
    Quantity new_quantity)
{
    validate_price(new_price);
    validate_quantity(new_quantity);

    const auto location_it = order_locations_.find(order_id);
    if (location_it == order_locations_.end()) {
        throw std::invalid_argument("Order ID does not exist.");
    }

    const OrderLocation location = location_it->second;
    const Quantity current_quantity = location.order->quantity;

    // A reduction at the same price keeps time priority.
    if (new_price == location.price && new_quantity <= current_quantity) {
        if (new_quantity == current_quantity) {
            return {};
        }

        const auto reduce_at_level = [&location, new_quantity](auto& levels) {
            const auto level_it = levels.find(location.price);
            if (level_it == levels.end()) {
                throw std::logic_error("Order location refers to a missing price level.");
            }
            level_it->second.reduce_order_quantity(location.order, new_quantity);
        };

        if (location.side == Side::buy) {
            reduce_at_level(bids_);
        } else if (location.side == Side::sell) {
            reduce_at_level(asks_);
        } else {
            throw std::logic_error("Order location contains an invalid side.");
        }

        return {};
    }

    // Price changes and quantity increases lose priority via cancel-and-replace.
    if (!cancel_order(order_id)) {
        throw std::logic_error("Order disappeared during amendment.");
    }

    return submit_limit_order(
        order_id,
        location.side,
        new_price,
        new_quantity,
        TimeInForce::gtc
    );
}

std::optional<Price> OrderBook::best_bid() const
{
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const
{
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

void OrderBook::rest_order(
    OrderId order_id,
    Side side,
    Price price,
    Quantity quantity)
{
    const auto add_to_level = [&](auto& levels) {
        PriceLevel& level = levels[price];
        const auto position = level.add_order(Order{order_id, quantity});
        order_locations_.emplace(
            order_id,
            OrderLocation{side, price, position}
        );
    };

    if (side == Side::buy) {
        add_to_level(bids_);
    } else {
        add_to_level(asks_);
    }
}
