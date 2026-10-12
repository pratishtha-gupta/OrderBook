#include "price_level.hpp"

#include <iterator>
#include <stdexcept>

PriceLevel::Iterator PriceLevel::add_order(const Order& order)
{
    if (order.quantity == 0) {
        throw std::invalid_argument("Order quantity cannot be zero.");
    }

    orders_.push_back(order);
    total_quantity_ += order.quantity;
    return std::prev(orders_.end());
}

void PriceLevel::erase_order(Iterator order)
{
    if (order == orders_.end()) {
        throw std::invalid_argument("Cannot erase the end iterator.");
    }

    total_quantity_ -= order->quantity;
    orders_.erase(order);
}

void PriceLevel::reduce_order_quantity(Iterator order, Quantity new_quantity)
{
    if (order == orders_.end()) {
        throw std::invalid_argument("Cannot amend the end iterator.");
    }
    if (new_quantity == 0) {
        throw std::invalid_argument("Order quantity cannot be zero.");
    }
    if (new_quantity > order->quantity) {
        throw std::invalid_argument("New quantity exceeds current quantity.");
    }

    total_quantity_ -= order->quantity - new_quantity;
    order->quantity = new_quantity;
}

void PriceLevel::apply_fill_front(Quantity executed_quantity)
{
    if (orders_.empty()) {
        throw std::runtime_error("No orders available to fill.");
    }
    if (executed_quantity == 0) {
        throw std::invalid_argument("Executed quantity cannot be zero.");
    }
    if (executed_quantity > orders_.front().quantity) {
        throw std::invalid_argument("Executed quantity exceeds resting quantity.");
    }

    orders_.front().quantity -= executed_quantity;
    total_quantity_ -= executed_quantity;

    if (orders_.front().quantity == 0) {
        orders_.pop_front();
    }
}

const Order& PriceLevel::front_order() const
{
    if (orders_.empty()) {
        throw std::runtime_error("No orders available at this price level.");
    }
    return orders_.front();
}

bool PriceLevel::empty() const
{
    return orders_.empty();
}

Quantity PriceLevel::total_quantity() const
{
    return total_quantity_;
}
