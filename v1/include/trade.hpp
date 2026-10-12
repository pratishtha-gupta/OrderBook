#pragma once

#include "order.hpp"

struct Trade {
    OrderId maker_order_id;
    OrderId taker_order_id;
    Price price;
    Quantity quantity;
};
