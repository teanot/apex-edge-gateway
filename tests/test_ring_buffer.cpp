#include "test_harness.hpp"
#include "lock_free_ring_buffer.hpp"
#include "telemetry_record.hpp"
#include <thread>
#include <vector>

TEST_CASE(RingBuffer_BasicPushPop) {
    apex::SPSCRingBuffer<int> ring(8);
    REQUIRE(ring.empty());
    REQUIRE(ring.size() == 0);
    REQUIRE(ring.capacity() == 8);

    REQUIRE(ring.push(42));
    REQUIRE(ring.push(43));
    REQUIRE(!ring.empty());
    REQUIRE(ring.size() == 2);

    int val = 0;
    REQUIRE(ring.pop(val));
    REQUIRE(val == 42);
    REQUIRE(ring.pop(val));
    REQUIRE(val == 43);
    REQUIRE(ring.empty());
    REQUIRE_FALSE(ring.pop(val));
}

TEST_CASE(RingBuffer_OverflowBehavior) {
    apex::SPSCRingBuffer<int> ring(4);
    REQUIRE(ring.capacity() == 4);

    REQUIRE(ring.push(1));
    REQUIRE(ring.push(2));
    REQUIRE(ring.push(3));
    REQUIRE(ring.push(4));

    // 5th push should fail and increment drop counter
    REQUIRE_FALSE(ring.push(5));
    REQUIRE(ring.dropped_count() == 1);

    int val = 0;
    REQUIRE(ring.pop(val));
    REQUIRE(val == 1);

    // Now push should succeed
    REQUIRE(ring.push(5));
    REQUIRE(ring.dropped_count() == 1);
}

TEST_CASE(RingBuffer_ConcurrentSPSCStress) {
    constexpr size_t count = 100000;
    apex::SPSCRingBuffer<size_t> ring(1024);

    std::thread producer([&]() {
        for (size_t i = 0; i < count; ++i) {
            while (!ring.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::vector<size_t> consumed;
    consumed.reserve(count);

    std::thread consumer([&]() {
        size_t val = 0;
        while (consumed.size() < count) {
            if (ring.pop(val)) {
                consumed.push_back(val);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(consumed.size() == count);
    for (size_t i = 0; i < count; ++i) {
        REQUIRE(consumed[i] == i);
    }
}
