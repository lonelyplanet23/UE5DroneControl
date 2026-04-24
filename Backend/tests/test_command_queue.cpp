#include <gtest/gtest.h>
#include "drone/command_queue.h"
#include <thread>

TEST(CommandQueueTest, PushAndPop)
{
    CommandQueue q(128);

    DroneControlPacket cmd;
    cmd.timestamp = 1000.0;
    cmd.x = 1.0f;
    cmd.y = 2.0f;
    cmd.z = 3.0f;
    cmd.mode = 1;

    q.Push(cmd);
    EXPECT_EQ(q.Size(), 1);

    DroneControlPacket out;
    EXPECT_TRUE(q.Pop(out));
    EXPECT_DOUBLE_EQ(out.timestamp, 1000.0);
    EXPECT_FLOAT_EQ(out.x, 1.0f);
    EXPECT_EQ(q.Size(), 0);
}

TEST(CommandQueueTest, PopFromEmpty)
{
    CommandQueue q(128);
    DroneControlPacket out;
    EXPECT_FALSE(q.Pop(out));
}

TEST(CommandQueueTest, FifoOrder)
{
    CommandQueue q(128);

    DroneControlPacket cmd1, cmd2;
    cmd1.timestamp = 1.0;
    cmd2.timestamp = 2.0;

    q.Push(cmd1);
    q.Push(cmd2);

    DroneControlPacket out;
    q.Pop(out);
    EXPECT_DOUBLE_EQ(out.timestamp, 1.0);
    q.Pop(out);
    EXPECT_DOUBLE_EQ(out.timestamp, 2.0);
}

TEST(CommandQueueTest, OverflowDropsOldest)
{
    CommandQueue q(2);  // 最多 2 个

    DroneControlPacket cmd;
    cmd.timestamp = 1.0; q.Push(cmd);
    cmd.timestamp = 2.0; q.Push(cmd);
    cmd.timestamp = 3.0; q.Push(cmd);  // 应丢弃 1.0

    EXPECT_EQ(q.Size(), 2);

    DroneControlPacket out;
    q.Pop(out);
    EXPECT_DOUBLE_EQ(out.timestamp, 2.0);  // 最早的 1.0 被丢弃
}

TEST(CommandQueueTest, PauseBlocksPop)
{
    CommandQueue q(128);

    DroneControlPacket cmd;
    cmd.timestamp = 1.0;
    q.Push(cmd);

    q.SetPaused(true);
    DroneControlPacket out;
    EXPECT_FALSE(q.Pop(out));  // 暂停时无法出队
    EXPECT_EQ(q.Size(), 1);

    q.SetPaused(false);
    EXPECT_TRUE(q.Pop(out));   // 恢复后可出队
}

TEST(CommandQueueTest, Clear)
{
    CommandQueue q(128);
    q.Push(DroneControlPacket{});
    q.Push(DroneControlPacket{});
    EXPECT_EQ(q.Size(), 2);

    q.Clear();
    EXPECT_EQ(q.Size(), 0);
}

TEST(CommandQueueTest, ThreadSafety)
{
    CommandQueue q(1024);
    constexpr int N = 10000;

    std::thread producer([&]() {
        for (int i = 0; i < N; ++i) {
            DroneControlPacket cmd;
            cmd.timestamp = i;
            q.Push(cmd);
        }
    });

    std::thread consumer([&]() {
        int count = 0;
        DroneControlPacket out;
        while (count < N) {
            if (q.Pop(out)) ++count;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(q.Size(), 0);
}
