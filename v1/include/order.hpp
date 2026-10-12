#pragma once

#include <cstdint>

using OrderId = std::uint64_t;
using Quantity = std::uint64_t;
using Price = std::uint64_t;

enum class Side {
    buy,
    sell
};

enum class TimeInForce {
    gtc, // Good 'Til Cancelled: rest any unfilled quantity.
    ioc, // Immediate or Cancel: discard any unfilled quantity.
    fok  // Fill or Kill: execute fully or do nothing.
};

struct Order {
    OrderId order_id;
    Quantity quantity;
};
