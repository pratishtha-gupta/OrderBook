#pragma once

#include "order.hpp"

#include <list>

// A FIFO queue of resting orders at one price.
class PriceLevel {
public:
    using Iterator = std::list<Order>::iterator;

    Iterator add_order(const Order& order);
    void erase_order(Iterator order);
    void reduce_order_quantity(Iterator order, Quantity new_quantity);
    void apply_fill_front(Quantity executed_quantity);

    [[nodiscard]] const Order& front_order() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] Quantity total_quantity() const;

private:
    std::list<Order> orders_;
    Quantity total_quantity_{0};
};
