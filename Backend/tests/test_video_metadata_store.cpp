#include "storage/video_metadata_store.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class VideoMetadataStoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto unique = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path()
            / ("ue5drone-video-metadata-test-" + std::to_string(unique));
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    boost::json::object MakeBatch(std::uint64_t sequence = 0) const
    {
        boost::json::array frames;
        frames.push_back(boost::json::object{
            {"frame_index", 1}, {"stream_generation", 1},
            {"stream_pts_ns", 0}, {"latitude", 30.0},
        });
        frames.push_back(boost::json::object{
            {"frame_index", 2}, {"stream_generation", 1},
            {"stream_pts_ns", 33333333}, {"latitude", 30.00001},
        });
        return boost::json::object{
            {"schema_version", 1},
            {"mission_id", "mission_20260721_drone_1"},
            {"drone_id", "1"},
            {"batch_sequence", sequence},
            {"final", false},
            {"frames", std::move(frames)},
            {"stream_path", "drone-1"},
        };
    }

    std::filesystem::path root_;
};

TEST_F(VideoMetadataStoreTest, StoresJsonLinesAndDeduplicatesRetry)
{
    VideoMetadataStore store(root_, 10);
    const auto first = store.AppendBatch(MakeBatch(7));
    EXPECT_EQ(first.accepted_frames, 2u);
    EXPECT_FALSE(first.duplicate);

    const auto retry = store.AppendBatch(MakeBatch(7));
    EXPECT_TRUE(retry.duplicate);

    const auto batch_path = root_ / first.relative_batch_path;
    ASSERT_TRUE(std::filesystem::exists(batch_path));
    std::ifstream input(batch_path);
    std::string line;
    int line_count = 0;
    while (std::getline(input, line)) {
        if (!line.empty()) ++line_count;
    }
    EXPECT_EQ(line_count, 2);
    EXPECT_TRUE(std::filesystem::exists(batch_path.parent_path().parent_path()
                                        / "session.json"));
}

TEST_F(VideoMetadataStoreTest, RejectsUnsafeMissionNameAndOversizedBatch)
{
    VideoMetadataStore store(root_, 1);
    auto unsafe = MakeBatch();
    unsafe["mission_id"] = "../escape";
    EXPECT_THROW(store.AppendBatch(unsafe), std::invalid_argument);

    EXPECT_THROW(store.AppendBatch(MakeBatch()), std::invalid_argument);
}

TEST_F(VideoMetadataStoreTest, AcceptsEmptyFinalBatchAndWritesCompletionMarker)
{
    VideoMetadataStore store(root_, 10);
    auto final_batch = MakeBatch(8);
    final_batch["frames"] = boost::json::array{};
    final_batch["final"] = true;
    final_batch["summary"] = boost::json::object{{"frames_received", 100}};

    const auto result = store.AppendBatch(final_batch);
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.accepted_frames, 0u);
    const auto mission_dir = (root_ / result.relative_batch_path)
        .parent_path().parent_path();
    EXPECT_TRUE(std::filesystem::exists(mission_dir / "completed.json"));
}

} // namespace
