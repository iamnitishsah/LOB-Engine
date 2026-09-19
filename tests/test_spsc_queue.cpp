#include <gtest/gtest.h>
#include "lob/spsc_queue.hpp"
#include <thread>
#include <vector>
#include <chrono>

TEST(SPSCQueueTest, BasicPushPop) {
    lob::SPSCQueue<int> queue(5);
    EXPECT_TRUE(queue.empty());

    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_TRUE(queue.push(3));

    EXPECT_FALSE(queue.empty());

    int val;
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 1);
    
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 2);
    
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 3);
    
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.pop(val));
}

TEST(SPSCQueueTest, PushUntilFull) {
    lob::SPSCQueue<int> queue(3);
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_TRUE(queue.push(3));
    EXPECT_FALSE(queue.push(4)); // Should be full
}

TEST(SPSCQueueTest, ProducerConsumerThreads) {
    const int NUM_ITEMS = 100000;
    lob::SPSCQueue<int> queue(1024);
    
    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        int expected = 0;
        int val = 0;
        while (expected < NUM_ITEMS) {
            if (queue.pop(val)) {
                EXPECT_EQ(val, expected);
                ++expected;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
}
