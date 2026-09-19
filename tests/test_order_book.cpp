#include <gtest/gtest.h>
#include "lob/order_book.hpp"

template <typename T>
class OrderBookTest : public ::testing::Test {
protected:
    void SetUp() override {
        book = std::make_unique<T>();
    }

    std::unique_ptr<lob::IOrderBook> book;
};

using BookTypes = ::testing::Types<lob::MapOrderBook, lob::FlatArrayOrderBook>;
TYPED_TEST_SUITE(OrderBookTest, BookTypes);

TYPED_TEST(OrderBookTest, EmptyBookBehavior) {
    EXPECT_EQ(this->book->get_best_bid(), lob::INVALID_PRICE);
    EXPECT_EQ(this->book->get_best_ask(), lob::INVALID_PRICE);
    EXPECT_EQ(this->book->get_spread(), lob::INVALID_PRICE);
    EXPECT_EQ(this->book->order_count(), 0);
    EXPECT_TRUE(this->book->verify_invariants());
}

TYPED_TEST(OrderBookTest, AddAndCancelSingleOrder) {
    auto* order = this->book->add_order(1, lob::Side::Buy, 100, 10);
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->id, 1);
    EXPECT_EQ(order->price, 100);
    EXPECT_EQ(order->qty, 10);
    EXPECT_EQ(order->side, lob::Side::Buy);

    EXPECT_EQ(this->book->get_best_bid(), 100);
    EXPECT_EQ(this->book->order_count(), 1);
    EXPECT_TRUE(this->book->verify_invariants());

    bool cancelled = this->book->cancel_order(1);
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(this->book->get_best_bid(), lob::INVALID_PRICE);
    EXPECT_EQ(this->book->order_count(), 0);
    EXPECT_TRUE(this->book->verify_invariants());
}

TYPED_TEST(OrderBookTest, MultiplePriceLevelsAndSpread) {
    this->book->add_order(1, lob::Side::Buy, 100, 10);
    this->book->add_order(2, lob::Side::Buy, 99, 20);
    this->book->add_order(3, lob::Side::Sell, 102, 15);
    this->book->add_order(4, lob::Side::Sell, 105, 25);

    EXPECT_EQ(this->book->get_best_bid(), 100);
    EXPECT_EQ(this->book->get_best_ask(), 102);
    EXPECT_EQ(this->book->get_spread(), 2);
    EXPECT_EQ(this->book->order_count(), 4);
    EXPECT_TRUE(this->book->verify_invariants());

    // Cancel best bid -> next best bid should be 99
    this->book->cancel_order(1);
    EXPECT_EQ(this->book->get_best_bid(), 99);
    EXPECT_EQ(this->book->get_spread(), 3);
    EXPECT_TRUE(this->book->verify_invariants());

    // Cancel best ask -> next best ask should be 105
    this->book->cancel_order(3);
    EXPECT_EQ(this->book->get_best_ask(), 105);
    EXPECT_EQ(this->book->get_spread(), 6);
    EXPECT_TRUE(this->book->verify_invariants());
}

TYPED_TEST(OrderBookTest, ModifyOrderQuantity) {
    this->book->add_order(1, lob::Side::Buy, 100, 20);
    this->book->add_order(2, lob::Side::Buy, 100, 30);

    const auto* level = this->book->get_best_bid_level();
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->total_qty, 50);
    EXPECT_EQ(level->order_count, 2);

    // Reduce quantity: preserves priority
    bool modified = this->book->modify_order(1, 15);
    EXPECT_TRUE(modified);
    EXPECT_EQ(level->total_qty, 45);
    EXPECT_EQ(level->head->id, 1);
    EXPECT_EQ(level->head->qty, 15);
    EXPECT_TRUE(this->book->verify_invariants());

    // Increase quantity: loses priority, re-queued at tail
    modified = this->book->modify_order(1, 25);
    EXPECT_TRUE(modified);
    EXPECT_EQ(level->total_qty, 55);
    EXPECT_EQ(level->head->id, 2); // order 2 is now first
    EXPECT_EQ(level->tail->id, 1); // order 1 is now last
    EXPECT_TRUE(this->book->verify_invariants());

    // Modify to 0 cancels
    modified = this->book->modify_order(2, 0);
    EXPECT_TRUE(modified);
    EXPECT_EQ(level->total_qty, 25);
    EXPECT_EQ(level->order_count, 1);
    EXPECT_EQ(this->book->order_count(), 1);
    EXPECT_TRUE(this->book->verify_invariants());
}

TYPED_TEST(OrderBookTest, DuplicateOrderIdRejected) {
    auto* o1 = this->book->add_order(1, lob::Side::Buy, 100, 10);
    EXPECT_NE(o1, nullptr);

    auto* o2 = this->book->add_order(1, lob::Side::Buy, 100, 10);
    EXPECT_EQ(o2, nullptr);
    EXPECT_EQ(this->book->order_count(), 1);
}
