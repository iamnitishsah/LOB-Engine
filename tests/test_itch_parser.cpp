#include <gtest/gtest.h>
#include "lob/itch_parser.hpp"

using namespace lob;

// Helper to easily write Big Endian data
inline void write_be16(uint8_t* ptr, uint16_t val) {
    ptr[0] = (val >> 8) & 0xFF;
    ptr[1] = val & 0xFF;
}

inline void write_be32(uint8_t* ptr, uint32_t val) {
    ptr[0] = (val >> 24) & 0xFF;
    ptr[1] = (val >> 16) & 0xFF;
    ptr[2] = (val >> 8) & 0xFF;
    ptr[3] = val & 0xFF;
}

inline void write_be64(uint8_t* ptr, uint64_t val) {
    ptr[0] = (val >> 56) & 0xFF;
    ptr[1] = (val >> 48) & 0xFF;
    ptr[2] = (val >> 40) & 0xFF;
    ptr[3] = (val >> 32) & 0xFF;
    ptr[4] = (val >> 24) & 0xFF;
    ptr[5] = (val >> 16) & 0xFF;
    ptr[6] = (val >> 8) & 0xFF;
    ptr[7] = val & 0xFF;
}

inline void write_ts(uint8_t* ptr, uint64_t ts) {
    ptr[0] = (ts >> 40) & 0xFF;
    ptr[1] = (ts >> 32) & 0xFF;
    ptr[2] = (ts >> 24) & 0xFF;
    ptr[3] = (ts >> 16) & 0xFF;
    ptr[4] = (ts >> 8) & 0xFF;
    ptr[5] = ts & 0xFF;
}

TEST(ITCHParserTest, ParseAddOrder) {
    uint8_t buffer[sizeof(itch::AddOrder)];
    std::memset(buffer, 0, sizeof(buffer));

    // Construct an AddOrder ('A') message manually
    buffer[0] = 'A';
    write_be16(buffer + 1, 100); // stock_locate
    write_be16(buffer + 3, 1234); // tracking_number
    write_ts(buffer + 5, 1600000000000ULL); // timestamp
    write_be64(buffer + 11, 42); // order_ref
    buffer[19] = 'B'; // side (Buy)
    write_be32(buffer + 20, 1000); // shares
    std::memcpy(buffer + 24, "AAPL    ", 8); // stock
    write_be32(buffer + 32, 1500000); // price (150.00)

    bool called = false;
    itch::Parser parser([&](const MarketEvent& ev) {
        EXPECT_EQ(ev.type, EventType::Add);
        EXPECT_EQ(ev.inst_id, 100);
        EXPECT_EQ(ev.order_id, 42);
        EXPECT_EQ(ev.side, Side::Buy);
        EXPECT_EQ(ev.qty, 1000);
        EXPECT_EQ(ev.price, 1500000);
        EXPECT_EQ(ev.timestamp, 1600000000000ULL);
        called = true;
    });

    EXPECT_TRUE(parser.parse_message(buffer, sizeof(buffer)));
    EXPECT_TRUE(called);
}

TEST(ITCHParserTest, ParseOrderCancel) {
    uint8_t buffer[sizeof(itch::OrderCancel)];
    std::memset(buffer, 0, sizeof(buffer));

    buffer[0] = 'X';
    write_be16(buffer + 1, 200); // stock_locate
    write_ts(buffer + 5, 9000); // ts
    write_be64(buffer + 11, 555); // order_ref
    write_be32(buffer + 19, 500); // canceled shares

    bool called = false;
    itch::Parser parser([&](const MarketEvent& ev) {
        EXPECT_EQ(ev.type, EventType::Cancel);
        EXPECT_EQ(ev.inst_id, 200);
        EXPECT_EQ(ev.order_id, 555);
        EXPECT_EQ(ev.qty, 500);
        EXPECT_EQ(ev.price, 0);
        called = true;
    });

    EXPECT_TRUE(parser.parse_message(buffer, sizeof(buffer)));
    EXPECT_TRUE(called);
}
