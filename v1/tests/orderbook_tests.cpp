#include "orderbook.hpp"

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

void test_buy_matches_by_price_then_time()
{
    OrderBook book;
    book.submit_limit_order(1, Side::sell, 100, 3, TimeInForce::gtc);
    book.submit_limit_order(2, Side::sell, 100, 4, TimeInForce::gtc);
    book.submit_limit_order(3, Side::sell, 101, 5, TimeInForce::gtc);

    const auto trades =
        book.submit_limit_order(10, Side::buy, 101, 10, TimeInForce::gtc);

    require(trades.size() == 3, "buy should match three makers");
    require(trades[0].maker_order_id == 1 && trades[0].price == 100 &&
                trades[0].quantity == 3,
            "oldest order at the best ask should execute first");
    require(trades[1].maker_order_id == 2 && trades[1].price == 100 &&
                trades[1].quantity == 4,
            "second order at the best ask should execute second");
    require(trades[2].maker_order_id == 3 && trades[2].price == 101 &&
                trades[2].quantity == 3,
            "matching should continue at the next crossed ask");
    require(book.best_ask() == 101, "partially filled ask should remain");
    require(!book.best_bid().has_value(), "fully filled buy should not rest");
}

void test_sell_matches_highest_bid_first()
{
    OrderBook book;
    book.submit_limit_order(1, Side::buy, 100, 2, TimeInForce::gtc);
    book.submit_limit_order(2, Side::buy, 99, 3, TimeInForce::gtc);

    const auto trades =
        book.submit_limit_order(10, Side::sell, 99, 4, TimeInForce::gtc);

    require(trades.size() == 2, "sell should match two bid levels");
    require(trades[0].maker_order_id == 1 && trades[0].price == 100 &&
                trades[0].quantity == 2,
            "sell should execute against the highest bid first");
    require(trades[1].maker_order_id == 2 && trades[1].price == 99 &&
                trades[1].quantity == 2,
            "sell should continue at the next crossed bid");
    require(book.best_bid() == 99, "partially filled bid should remain");
}

void test_ioc_discards_unfilled_quantity()
{
    OrderBook book;
    book.submit_limit_order(1, Side::sell, 100, 2, TimeInForce::gtc);

    const auto trades =
        book.submit_limit_order(10, Side::buy, 100, 5, TimeInForce::ioc);

    require(trades.size() == 1 && trades[0].quantity == 2,
            "IOC should execute available crossing quantity");
    require(!book.best_ask().has_value(), "filled ask should be removed");
    require(!book.best_bid().has_value(), "IOC remainder should not rest");
}

void test_fok_is_atomic()
{
    OrderBook book;
    book.submit_limit_order(1, Side::sell, 100, 2, TimeInForce::gtc);
    book.submit_limit_order(2, Side::sell, 101, 2, TimeInForce::gtc);

    const auto rejected =
        book.submit_limit_order(10, Side::buy, 100, 3, TimeInForce::fok);

    require(rejected.empty(), "unfillable FOK should produce no trades");
    require(book.best_ask() == 100, "unfillable FOK should not mutate the book");

    const auto accepted =
        book.submit_limit_order(11, Side::buy, 101, 4, TimeInForce::fok);

    require(accepted.size() == 2,
            "fillable FOK should consume all required liquidity");
    require(accepted[0].quantity == 2 && accepted[1].quantity == 2,
            "fillable FOK should fill its complete quantity");
    require(!book.best_ask().has_value(),
            "fillable FOK should remove consumed levels");
}

void test_sell_fok_respects_its_limit()
{
    OrderBook book;
    book.submit_limit_order(1, Side::buy, 101, 2, TimeInForce::gtc);
    book.submit_limit_order(2, Side::buy, 100, 2, TimeInForce::gtc);

    const auto rejected =
        book.submit_limit_order(10, Side::sell, 101, 3, TimeInForce::fok);
    require(rejected.empty(),
            "sell FOK should ignore bids below its limit");
    require(book.best_bid() == 101,
            "rejected sell FOK should not mutate the book");

    const auto accepted =
        book.submit_limit_order(11, Side::sell, 100, 4, TimeInForce::fok);
    require(accepted.size() == 2,
            "sell FOK should fill across eligible bid levels");
    require(!book.best_bid().has_value(),
            "filled sell FOK should remove consumed bids");
}

void test_cancel_removes_order_and_empty_level()
{
    OrderBook book;
    book.submit_limit_order(1, Side::buy, 100, 5, TimeInForce::gtc);
    book.submit_limit_order(2, Side::buy, 101, 3, TimeInForce::gtc);

    require(book.cancel_order(2), "existing order should be cancelled");
    require(book.best_bid() == 100,
            "cancelling the best bid should expose the next bid");
    require(!book.cancel_order(2),
            "cancelling an unknown order should report failure");
    require(book.cancel_order(1), "remaining bid should be cancellable");
    require(!book.best_bid().has_value(),
            "cancelling the final bid should remove its price level");
}

void test_cancel_preserves_fifo_and_aggregate_quantity()
{
    OrderBook book;
    book.submit_limit_order(1, Side::sell, 100, 2, TimeInForce::gtc);
    book.submit_limit_order(2, Side::sell, 100, 3, TimeInForce::gtc);
    book.submit_limit_order(3, Side::sell, 100, 4, TimeInForce::gtc);

    require(book.cancel_order(2), "middle order should be cancellable");

    const auto rejected =
        book.submit_limit_order(10, Side::buy, 100, 7, TimeInForce::fok);
    require(rejected.empty(),
            "cancelled quantity must not count toward FOK liquidity");

    const auto trades =
        book.submit_limit_order(11, Side::buy, 100, 6, TimeInForce::fok);
    require(trades.size() == 2, "remaining orders should satisfy the FOK");
    require(trades[0].maker_order_id == 1 &&
                trades[1].maker_order_id == 3,
            "cancelling a middle order should preserve FIFO priority");
}

void test_quantity_reduction_preserves_priority()
{
    OrderBook book;
    book.submit_limit_order(1, Side::sell, 100, 5, TimeInForce::gtc);
    book.submit_limit_order(2, Side::sell, 100, 5, TimeInForce::gtc);

    const auto amend_trades = book.amend_order(1, 100, 2);
    require(amend_trades.empty(), "quantity reduction should not trade");

    const auto trades =
        book.submit_limit_order(10, Side::buy, 100, 3, TimeInForce::fok);
    require(trades.size() == 2,
            "reduced order and next maker should fill the incoming order");
    require(trades[0].maker_order_id == 1 && trades[0].quantity == 2,
            "reduced order should retain FIFO priority");
    require(trades[1].maker_order_id == 2 && trades[1].quantity == 1,
            "next maker should supply the remaining quantity");
}

void test_quantity_increase_loses_priority()
{
    OrderBook book;
    book.submit_limit_order(1, Side::sell, 100, 2, TimeInForce::gtc);
    book.submit_limit_order(2, Side::sell, 100, 2, TimeInForce::gtc);

    const auto amend_trades = book.amend_order(1, 100, 3);
    require(amend_trades.empty(), "same-price quantity increase should not trade");

    const auto trades =
        book.submit_limit_order(10, Side::buy, 100, 2, TimeInForce::fok);
    require(trades.size() == 1 && trades[0].maker_order_id == 2,
            "increased order should move behind existing orders");
}

void test_price_change_can_cross_the_book()
{
    OrderBook book;
    book.submit_limit_order(1, Side::buy, 99, 3, TimeInForce::gtc);
    book.submit_limit_order(2, Side::sell, 100, 2, TimeInForce::gtc);

    const auto trades = book.amend_order(1, 100, 3);

    require(trades.size() == 1,
            "price amendment should match newly crossed liquidity");
    require(trades[0].maker_order_id == 2 && trades[0].taker_order_id == 1 &&
                trades[0].price == 100 && trades[0].quantity == 2,
            "price amendment should execute as a replacement order");
    require(book.best_bid() == 100,
            "unfilled replacement quantity should rest at its new price");
    require(!book.best_ask().has_value(), "crossed ask should be removed");
}

void test_market_buy_walks_the_ask_book()
{
    OrderBook book;
    book.submit_limit_order(1, Side::sell, 100, 2, TimeInForce::gtc);
    book.submit_limit_order(2, Side::sell, 101, 3, TimeInForce::gtc);

    const auto trades = book.submit_market_order(10, Side::buy, 8);

    require(trades.size() == 2, "market buy should consume both ask levels");
    require(trades[0].maker_order_id == 1 && trades[0].price == 100 &&
                trades[0].quantity == 2,
            "market buy should consume the best ask first");
    require(trades[1].maker_order_id == 2 && trades[1].price == 101 &&
                trades[1].quantity == 3,
            "market buy should continue at the next ask");
    require(!book.best_ask().has_value(), "consumed asks should be removed");
    require(!book.best_bid().has_value(),
            "unfilled market quantity should never rest");
}

void test_market_sell_walks_the_bid_book()
{
    OrderBook book;
    book.submit_limit_order(1, Side::buy, 101, 2, TimeInForce::gtc);
    book.submit_limit_order(2, Side::buy, 100, 3, TimeInForce::gtc);

    const auto trades = book.submit_market_order(10, Side::sell, 4);

    require(trades.size() == 2, "market sell should consume two bid levels");
    require(trades[0].maker_order_id == 1 && trades[0].price == 101 &&
                trades[0].quantity == 2,
            "market sell should consume the best bid first");
    require(trades[1].maker_order_id == 2 && trades[1].price == 100 &&
                trades[1].quantity == 2,
            "market sell should continue at the next bid");
    require(book.best_bid() == 100, "partially filled bid should remain");
}

void test_market_order_against_empty_book_is_discarded()
{
    OrderBook book;

    const auto trades = book.submit_market_order(1, Side::buy, 5);

    require(trades.empty(), "empty book should produce no market trades");
    require(!book.best_bid().has_value() && !book.best_ask().has_value(),
            "market order should not create a resting level");
}

void test_partial_fill_can_be_cancelled_and_id_reused()
{
    OrderBook book;
    book.submit_limit_order(1, Side::sell, 100, 5, TimeInForce::gtc);
    book.submit_market_order(10, Side::buy, 2);

    require(book.cancel_order(1),
            "partially filled resting order should remain cancellable");
    require(!book.best_ask().has_value(),
            "cancelling a partial remainder should remove its level");

    book.submit_limit_order(1, Side::sell, 101, 1, TimeInForce::gtc);
    require(book.best_ask() == 101,
            "cancelled order ID should be reusable once inactive");
}

void test_invalid_requests_do_not_mutate_book()
{
    OrderBook book;
    book.submit_limit_order(1, Side::buy, 100, 5, TimeInForce::gtc);

    require_throws<std::invalid_argument>(
        [&book] { book.submit_limit_order(1, Side::sell, 101, 1,
                                          TimeInForce::gtc); },
        "duplicate active order ID should be rejected");
    require_throws<std::invalid_argument>(
        [&book] { book.submit_limit_order(2, Side::buy, 0, 1,
                                          TimeInForce::gtc); },
        "zero limit price should be rejected");
    require_throws<std::invalid_argument>(
        [&book] { book.submit_limit_order(2, Side::buy, 100, 0,
                                          TimeInForce::gtc); },
        "zero limit quantity should be rejected");
    require_throws<std::invalid_argument>(
        [&book] { book.submit_limit_order(
            2, static_cast<Side>(99), 100, 1, TimeInForce::gtc); },
        "invalid limit side should be rejected");
    require_throws<std::invalid_argument>(
        [&book] { book.submit_limit_order(
            2, Side::buy, 100, 1, static_cast<TimeInForce>(99)); },
        "invalid time in force should be rejected");
    require_throws<std::invalid_argument>(
        [&book] { book.submit_market_order(2, Side::buy, 0); },
        "zero-quantity market order should be rejected");
    require_throws<std::invalid_argument>(
        [&book] { book.submit_market_order(1, Side::sell, 1); },
        "market order should reject a duplicate active ID");
    require_throws<std::invalid_argument>(
        [&book] { book.submit_market_order(
            2, static_cast<Side>(99), 1); },
        "invalid market side should be rejected");
    require_throws<std::invalid_argument>(
        [&book] { book.amend_order(99, 100, 1); },
        "unknown amendment ID should be rejected");
    require_throws<std::invalid_argument>(
        [&book] { book.amend_order(1, 0, 1); },
        "zero amendment price should be rejected");
    require_throws<std::invalid_argument>(
        [&book] { book.amend_order(1, 100, 0); },
        "zero amendment quantity should be rejected");

    require(book.best_bid() == 100,
            "rejected operations should leave the book unchanged");
}

} // namespace

int main()
{
    try {
        test_buy_matches_by_price_then_time();
        test_sell_matches_highest_bid_first();
        test_ioc_discards_unfilled_quantity();
        test_fok_is_atomic();
        test_sell_fok_respects_its_limit();
        test_cancel_removes_order_and_empty_level();
        test_cancel_preserves_fifo_and_aggregate_quantity();
        test_quantity_reduction_preserves_priority();
        test_quantity_increase_loses_priority();
        test_price_change_can_cross_the_book();
        test_market_buy_walks_the_ask_book();
        test_market_sell_walks_the_bid_book();
        test_market_order_against_empty_book_is_discarded();
        test_partial_fill_can_be_cancelled_and_id_reused();
        test_invalid_requests_do_not_mutate_book();

        std::cout << "All OrderBook tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "OrderBook test failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
