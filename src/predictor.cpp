#include "predictor.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace valkyrie {

// Static pattern detection using regex
std::optional<std::string> Predictor::predict_next_sequential(const std::string& filename) {
    // Pattern: prefix + number + suffix
    // Example: "shard_042.bin" -> prefix="shard_", number=42, suffix=".bin"
    // Use non-greedy (.*?) to avoid capturing trailing digits in the prefix
    std::regex pattern(R"(^(.*?)(\d+)(\..*)$)");
    std::smatch matches;

    if (!std::regex_match(filename, matches, pattern)) {
        return std::nullopt;  // No numeric pattern
    }

    std::string prefix = matches[1].str();
    std::string number_str = matches[2].str();
    std::string suffix = matches[3].str();

    // Parse number - handle exceptions for malformed or out-of-range numbers
    try {
        int number = std::stoi(number_str);
        int next_number = number + 1;

        // Preserve padding (e.g., 042 -> 043, not 43)
        int padding = number_str.length();
        std::ostringstream oss;
        oss << prefix << std::setw(padding) << std::setfill('0') << next_number << suffix;

        return oss.str();
    } catch (const std::exception&) {
        return std::nullopt;  // Invalid numeric sequence
    }
}

Predictor::Predictor(CacheManager& cache,
                     S3WorkerPool& worker_pool,
                     int lookahead)
    : cache_(cache)
    , worker_pool_(worker_pool)
    , lookahead_(lookahead)
    , manifest_mode_(false)
    , stop_flag_(false) {
}

Predictor::~Predictor() {
    stop();
}

void Predictor::start() {
    predictor_thread_ = std::thread(&Predictor::predictor_loop, this);
    std::cout << "Predictor: Started with lookahead=" << lookahead_ << "\n";
}

void Predictor::stop() {
    if (stop_flag_.exchange(true)) {
        return;  // Already stopped
    }

    if (predictor_thread_.joinable()) {
        predictor_thread_.join();
    }

    std::cout << "Predictor: Stopped\n";
}

void Predictor::on_file_accessed(const std::string& s3_key) {
    std::lock_guard<std::mutex> lock(access_mutex_);
    last_accessed_ = s3_key;
}

bool Predictor::load_manifest(const std::string& manifest_path) {
    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        std::cerr << "Predictor: Failed to open manifest: " << manifest_path << "\n";
        return false;
    }

    manifest_.clear();
    std::string line;
    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (!line.empty() && line[0] != '#') {  // Skip empty and comments
            manifest_.push_back(line);
        }
    }

    manifest_mode_ = !manifest_.empty();

    std::cout << "Predictor: Loaded manifest with " << manifest_.size() << " entries\n";
    return true;
}

void Predictor::predictor_loop() {
    while (!stop_flag_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Clean up completed downloads to prevent memory leak
        cleanup_completed_downloads();

        std::string current;
        {
            std::lock_guard<std::mutex> lock(access_mutex_);
            if (last_accessed_.empty()) continue;
            current = last_accessed_;
        }

        predict_and_prefetch(current);
    }
}

void Predictor::predict_and_prefetch(const std::string& s3_key) {
    stats_.predictions_made++;

    std::vector<std::string> to_prefetch;

    if (manifest_mode_) {
        // Manifest-driven prediction
        auto pos_opt = find_in_manifest(s3_key);
        if (!pos_opt.has_value()) return;

        int pos = *pos_opt;
        for (int i = 1; i <= lookahead_; ++i) {
            int next_pos = pos + i;
            if (next_pos < static_cast<int>(manifest_.size())) {
                to_prefetch.push_back(manifest_[next_pos]);
            }
        }

        if (!to_prefetch.empty()) {
            stats_.manifest_hits++;
        }

    } else {
        // Sequential pattern prediction
        std::string current = s3_key;
        for (int i = 0; i < lookahead_; ++i) {
            auto next_opt = predict_next_sequential(current);
            if (!next_opt.has_value()) break;

            to_prefetch.push_back(*next_opt);
            current = *next_opt;
        }

        if (!to_prefetch.empty()) {
            stats_.pattern_hits++;
        }
    }

    // Issue prefetch tasks
    for (const auto& file_key : to_prefetch) {
        // Skip if already in cache
        if (cache_.contains(file_key)) continue;

        // Skip if already in flight
        {
            std::lock_guard<std::mutex> lock(in_flight_mutex_);
            if (in_flight_.count(file_key)) continue;
            in_flight_.insert(file_key);
        }

        // Submit prefetch and track the future for cleanup
        auto future = worker_pool_.submit(file_key, 0, DEFAULT_CHUNK_SIZE, Priority::NORMAL);

        {
            std::lock_guard<std::mutex> lock(in_flight_mutex_);
            in_flight_futures_.emplace_back(file_key, future);
        }

        stats_.prefetches_issued++;
    }
}

void Predictor::cleanup_completed_downloads() {
    std::lock_guard<std::mutex> lock(in_flight_mutex_);

    // Remove completed downloads from tracking
    auto it = in_flight_futures_.begin();
    while (it != in_flight_futures_.end()) {
        const auto& [file_key, future] = *it;

        // Check if future is ready (completed or failed)
        if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            // Remove from in_flight_ set
            in_flight_.erase(file_key);
            // Remove from futures vector
            it = in_flight_futures_.erase(it);
        } else {
            ++it;
        }
    }
}

std::optional<int> Predictor::find_in_manifest(const std::string& s3_key) {
    for (size_t i = 0; i < manifest_.size(); ++i) {
        if (manifest_[i] == s3_key) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

}  // namespace valkyrie
