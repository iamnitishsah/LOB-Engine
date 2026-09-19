#include <gtest/gtest.h>
#include "lob/matching_engine.hpp"

class MatchingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto book = std::make_unique<lob::FlatArrayOrderBook>(20000);
        engine = std::make_unique<lob::MatchingEngine>(std::move(book));
        trades.clear();

        engine->set_trade_callback([this](const lob::TradeEvent& t) {
            trades.push_back(t);
        });
    }

    std::unique_ptr<lob::MatchingEngine> engine;
    std::vector<lob::TradeEvent> trades;
};

TEST_F(MatchingEngineTest, FullMatchSingleLevel) {
    // Resting ask: ID 1, Sell 100 @ 105
    engine->process_limit_order(1, lob::Side::Sell, 105, 100);
    EXPECT_EQ(engine->book().order_count(), 1);
    EXPECT_EQ(trades.size(), 0);

    // Incoming bid: ID 2, Buy 100 @ 105
    engine->process_limit_order(2, lob::Side::Buy, 105, 100);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].maker_order_id, 1);
    EXPECT_EQ(trades[0].taker_order_id, 2);
    EXPECT_EQ(trades[0].price, 105);
    EXPECT_EQ(trades[0].qty, 100);
    EXPECT_EQ(trades[0].taker_side, lob::Side::Buy);

    EXPECT_EQ(engine->book().order_count(), 0);
    EXPECT_TRUE(engine->book().verify_invariants());
}

TEST_F(MatchingEngineTest, PartialFillsAndRestingRemainder) {
    // Resting ask: ID 1, Sell 40 @ 105
    engine->process_limit_order(1, lob::Side::Sell, 105, 40);

    // Incoming bid: ID 2, Buy 100 @ 105
    // Should match 40 @ 105, rest 60 @ 105 on bid side
    engine->process_limit_order(2, lob::Side::Buy, 105, 100);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].maker_order_id, 1);
    EXPECT_EQ(trades[0].taker_order_id, 2);
    EXPECT_EQ(trades[0].qty, 40);

    EXPECT_EQ(engine->book().order_count(), 1);
    EXPECT_EQ(engine->book().get_best_bid(), 105);
    EXPECT_EQ(engine->book().get_best_ask(), lob::INVALID_PRICE);

    const auto* bid_level = engine->book().get_best_bid_level();
    ASSERT_NE(bid_level, nullptr);
    EXPECT_EQ(bid_level->total_qty, 60);
    EXPECT_TRUE(engine->book().verify_invariants());
}

TEST_F(MatchingEngineTest, PriceTimePriority) {
    // Two orders at same price: ID 1 arrives before ID 2
    engine->process_limit_order(1, lob::Side::Sell, 105, 50);
    engine->process_limit_order(2, lob::Side::Sell, 105, 50);

    // Incoming bid matches 60: must fill ID 1 completely (50), then ID 2 partially (10)
    engine->process_limit_order(3, lob::Side::Buy, 105, 60);

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].maker_order_id, 1);
    EXPECT_EQ(trades[0].qty, 50);

    EXPECT_EQ(trades[1].maker_order_id, 2);
    EXPECT_EQ(trades[1].qty, 10);

    // 40 remaining on ID 2
    EXPECT_EQ(engine->book().order_count(), 1);
    const auto* ask_level = engine->book().get_best_ask_level();
    ASSERT_NE(ask_level, nullptr);
    EXPECT_EQ(ask_level->head->id, 2);
    EXPECT_EQ(ask_level->head->qty, 40);
    EXPECT_TRUE(engine->book().verify_invariants());
}

TEST_F(MatchingEngineTest, MarketOrderSweepMultipleLevels) {
    engine->process_limit_order(1, lob::Side::Sell, 101, 10);
    engine->process_limit_order(2, lob::Side::Sell, 102, 20);
    engine->process_limit_order(3, lob::Side::Sell, 103, 30);

    // Market buy of 45: sweeps 101 (10), 102 (20), and 15 of 103
    lob::Qty filled = engine->process_market_order(4, lob::Side::Buy, 45);

    EXPECT_EQ(filled, 45);
    ASSERT_EQ(trades.size(), 3);
    EXPECT_EQ(trades[0].price, 101);
    EXPECT_EQ(trades[0].qty, 10);
    EXPECT_EQ(trades[1].price, 102);
    EXPECT_EQ(trades[1].qty, 20);
    EXPECT_EQ(trades[2].price, 103);
    EXPECT_EQ(trades[2].qty, 15);

    EXPECT_EQ(engine->book().get_best_ask(), 103);
    EXPECT_EQ(engine->book().get_best_ask_level()->total_qty, 15);
    EXPECT_TRUE(engine->book().verify_invariants());
}
