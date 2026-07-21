#pragma once

#include <boost/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

struct VideoMetadataStoreResult {
    std::string mission_id;
    std::string drone_id;
    std::string relative_batch_path;
    std::uint64_t batch_sequence = 0;
    std::size_t accepted_frames = 0;
    bool duplicate = false;
    bool completed = false;
};

// Persists immutable, idempotent metadata batches received from a Jetson.
// Each batch is a separate JSONL file, so a retry after a lost HTTP response
// cannot append the same frame twice.
class VideoMetadataStore {
public:
    explicit VideoMetadataStore(std::filesystem::path root_path,
                                std::size_t max_batch_frames = 300);

    VideoMetadataStoreResult AppendBatch(const boost::json::object& body);

private:
    static std::string RequireSafeComponent(const boost::json::object& body,
                                            const char* key);
    static std::uint64_t RequireSequence(const boost::json::object& body);
    static void WriteJsonAtomic(const std::filesystem::path& path,
                                const boost::json::value& value);

    std::filesystem::path root_path_;
    std::size_t max_batch_frames_;
    std::mutex write_mutex_;
};
