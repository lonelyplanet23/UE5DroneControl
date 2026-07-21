#include "storage/video_metadata_store.h"

#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

bool json_bool(const boost::json::object& body, const char* key, bool fallback)
{
    const auto it = body.find(key);
    return it != body.end() && it->value().is_bool()
        ? it->value().as_bool()
        : fallback;
}

std::string sequence_filename(std::uint64_t sequence)
{
    std::ostringstream name;
    name << std::setfill('0') << std::setw(12) << sequence << ".jsonl";
    return name.str();
}

} // namespace

VideoMetadataStore::VideoMetadataStore(std::filesystem::path root_path,
                                       std::size_t max_batch_frames)
    : root_path_(std::move(root_path))
    , max_batch_frames_(max_batch_frames)
{
    if (root_path_.empty()) {
        throw std::invalid_argument("video metadata storage path must not be empty");
    }
    if (max_batch_frames_ == 0) {
        throw std::invalid_argument("video metadata max batch must be positive");
    }
}

std::string VideoMetadataStore::RequireSafeComponent(
    const boost::json::object& body, const char* key)
{
    const auto it = body.find(key);
    if (it == body.end() || !it->value().is_string()) {
        throw std::invalid_argument(std::string(key) + " must be a string");
    }
    const std::string value(it->value().as_string());
    if (value.empty() || value.size() > 96) {
        throw std::invalid_argument(std::string(key) + " has invalid length");
    }
    for (const unsigned char ch : value) {
        if (!std::isalnum(ch) && ch != '-' && ch != '_' && ch != '.') {
            throw std::invalid_argument(
                std::string(key) + " contains an unsafe character");
        }
    }
    if (value == "." || value == "..") {
        throw std::invalid_argument(std::string(key) + " is not allowed");
    }
    return value;
}

std::uint64_t VideoMetadataStore::RequireSequence(
    const boost::json::object& body)
{
    const auto it = body.find("batch_sequence");
    if (it == body.end()) {
        throw std::invalid_argument("batch_sequence is required");
    }
    if (it->value().is_uint64()) return it->value().as_uint64();
    if (it->value().is_int64() && it->value().as_int64() >= 0) {
        return static_cast<std::uint64_t>(it->value().as_int64());
    }
    throw std::invalid_argument("batch_sequence must be a non-negative integer");
}

void VideoMetadataStore::WriteJsonAtomic(
    const std::filesystem::path& path, const boost::json::value& value)
{
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output << boost::json::serialize(value);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::rename(temporary, path);
}

VideoMetadataStoreResult VideoMetadataStore::AppendBatch(
    const boost::json::object& body)
{
    const auto mission_id = RequireSafeComponent(body, "mission_id");
    const auto drone_id = RequireSafeComponent(body, "drone_id");
    const auto batch_sequence = RequireSequence(body);
    const bool final_batch = json_bool(body, "final", false);

    const auto frames_it = body.find("frames");
    if (frames_it == body.end() || !frames_it->value().is_array()) {
        throw std::invalid_argument("frames must be an array");
    }
    const auto& frames = frames_it->value().as_array();
    if (frames.size() > max_batch_frames_) {
        throw std::invalid_argument("metadata batch exceeds configured frame limit");
    }
    if (frames.empty() && !final_batch) {
        throw std::invalid_argument("a non-final metadata batch must contain frames");
    }
    for (const auto& frame : frames) {
        if (!frame.is_object()) {
            throw std::invalid_argument("every frames entry must be an object");
        }
    }

    std::lock_guard<std::mutex> lock(write_mutex_);
    const auto mission_directory = root_path_ / ("drone-" + drone_id) / mission_id;
    const auto batch_directory = mission_directory / "batches";
    std::filesystem::create_directories(batch_directory);
    const auto batch_path = batch_directory / sequence_filename(batch_sequence);

    VideoMetadataStoreResult result;
    result.mission_id = mission_id;
    result.drone_id = drone_id;
    result.batch_sequence = batch_sequence;
    result.accepted_frames = frames.size();
    result.completed = final_batch;
    result.relative_batch_path = std::filesystem::relative(
        batch_path, root_path_).generic_string();

    if (std::filesystem::exists(batch_path)) {
        result.duplicate = true;
    } else {
        const auto temporary_batch = batch_path.string() + ".tmp";
        {
            std::ofstream output(
                temporary_batch, std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::failbit | std::ios::badbit);
            for (const auto& frame : frames) {
                output << boost::json::serialize(frame) << '\n';
            }
        }
        std::filesystem::rename(temporary_batch, batch_path);
    }

    boost::json::object session{
        {"schema_version", body.if_contains("schema_version")
            ? *body.if_contains("schema_version") : boost::json::value(1)},
        {"mission_id", mission_id},
        {"drone_id", drone_id},
        {"last_batch_sequence", batch_sequence},
        {"completed", final_batch},
    };
    for (const char* key : {"stream_path", "rtsp_url", "video_url",
                            "camera_info", "summary", "sent_at_unix_ns"}) {
        if (const auto* value = body.if_contains(key)) session[key] = *value;
    }
    WriteJsonAtomic(mission_directory / "session.json", session);

    if (final_batch) {
        WriteJsonAtomic(mission_directory / "completed.json", session);
    }

    return result;
}
