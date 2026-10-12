#include "price_level.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string{message});
    }
}

template <typename Exception, typename Function>
void require_throws(Function&& function, std::string_view message)
{
    try {
        function();
    } catch (const Exception&) {
        return;
    } catch (...) {
        throw std::runtime_error(
            std::string{message} + " (wrong exception type)");
    }

    throw std::runtime_error(std::string{message} + " (no exception thrown)");
}

void test_new_level_is_empty()
{
    const PriceLevel level;

    require(level.empty(), "a new level should be empty");
    require(level.total_quantity() == 0,
            "a new level should have zero aggregate quantity");
}

void test_add_order_updates_state()
{
    PriceLevel level;
    level.add_order(Order{1, 10});

    require(!level.empty(), "a level with an order should not be empty");
    require(level.total_quantity() == 10,
            "adding an order should update aggregate quantity");
    require(level.front_order().order_id == 1,
            "the added order should be at the front");
    require(level.front_order().quantity == 10,
            "the front order should retain its quantity");
}

void test_orders_have_fifo_priority()
{
    PriceLevel level;
    level.add_order(Order{1, 10});
    level.add_order(Order{2, 20});

    require(level.front_order().order_id == 1,
            "the oldest order should remain at the front");

    level.apply_fill_front(10);

    require(level.front_order().order_id == 2,
            "the second order should become front after the first is filled");
    require(level.total_quantity() == 20,
            "aggregate quantity should exclude the filled order");
}

void test_erase_order_updates_state()
{
    PriceLevel level;
    const auto first = level.add_order(Order{1, 10});
    level.add_order(Order{2, 5});

    level.erase_order(first);

    require(level.front_order().order_id == 2,
            "erasing an order should remove the selected order");
    require(level.total_quantity() == 5,
            "erasing an order should update aggregate quantity");
}

void test_reduce_order_quantity_updates_state()
{
    PriceLevel level;
    const auto order = level.add_order(Order{1, 10});
    level.add_order(Order{2, 5});

    level.reduce_order_quantity(order, 4);

    require(level.front_order().order_id == 1,
            "reducing quantity should preserve order priority");
    require(level.front_order().quantity == 4,
            "reducing quantity should update the order");
    require(level.total_quantity() == 9,
            "reducing quantity should update aggregate quantity");
}

void test_partial_fill_preserves_priority()
{
    PriceLevel level;
    level.add_order(Order{1, 10});
    level.add_order(Order{2, 5});

    level.apply_fill_front(4);

    require(level.front_order().order_id == 1,
            "a partially filled order should retain priority");
    require(level.front_order().quantity == 6,
            "a partial fill should reduce remaining order quantity");
    require(level.total_quantity() == 11,
            "a partial fill should reduce aggregate quantity");
}

void test_complete_fill_can_empty_level()
{
    PriceLevel level;
    level.add_order(Order{1, 7});

    level.apply_fill_front(7);

    require(level.empty(), "a fully consumed level should be empty");
    require(level.total_quantity() == 0,
            "a fully consumed level should have zero aggregate quantity");
}

void test_invalid_operations_do_not_mutate_level()
{
    PriceLevel level;

    require_throws<std::invalid_argument>(
        [&level] { level.add_order(Order{1, 0}); },
        "a zero-quantity order should be rejected");
    require(level.empty(), "a rejected order should not modify the level");

    require_throws<std::runtime_error>(
        [&level] { level.apply_fill_front(1); },
        "filling an empty level should be rejected");

    const auto order = level.add_order(Order{2, 5});
    require_throws<std::invalid_argument>(
        [&level] { level.apply_fill_front(0); },
        "a zero-quantity fill should be rejected");
    require_throws<std::invalid_argument>(
        [&level] { level.apply_fill_front(6); },
        "an overfill should be rejected");
    require_throws<std::invalid_argument>(
        [&level, order] { level.reduce_order_quantity(order, 0); },
        "a zero-quantity amendment should be rejected");
    require_throws<std::invalid_argument>(
        [&level, order] { level.reduce_order_quantity(order, 6); },
        "a quantity increase should not use the reduction path");

    require(level.front_order().quantity == 5,
            "rejected fills should not change the front order");
    require(level.total_quantity() == 5,
            "rejected fills should not change aggregate quantity");
}

} // namespace

int main()
{
    try {
        test_new_level_is_empty();
        test_add_order_updates_state();
        test_orders_have_fifo_priority();
        test_erase_order_updates_state();
        test_reduce_order_quantity_updates_state();
        test_partial_fill_preserves_priority();
        test_complete_fill_can_empty_level();
        test_invalid_operations_do_not_mutate_level();

        std::cout << "All PriceLevel tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "PriceLevel test failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
