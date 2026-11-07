// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <linux/fs.h>
#include <nlohmann/json.hpp>
#include <unistd.h>

module nom.md_logger_ai;

import std;

namespace nom
{
    constexpr std::size_t batch_bytes = 1024 * 1024;

    void system_failure(std::string_view action)
    {
        throw std::system_error(errno, std::generic_category(), std::string(action));
    }

    void write_bytes(int descriptor, std::span<const char> bytes)
    {
        while (!bytes.empty())
        {
            const auto written = ::write(descriptor, bytes.data(), bytes.size());
            if (written < 0)
            {
                if (errno == EINTR)
                    continue;
                system_failure("write capture segment");
            }
            if (written == 0)
                throw std::runtime_error("write capture segment made no progress");
            bytes = bytes.subspan(static_cast<std::size_t>(written));
        }
    }

    void synchronize(int descriptor)
    {
        while (::fsync(descriptor) != 0)
        {
            if (errno != EINTR)
                system_failure("synchronize capture storage");
        }
    }

    std::string timestamp_text(std::int64_t value)
    {
        std::array<char, 20> bytes;
        const auto result = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
        return {bytes.data(), result.ptr};
    }

    std::expected<std::size_t, capture_result> text_size(std::string_view text, std::size_t limit)
    {
        if (text.size() > limit)
            return std::unexpected(capture_result::too_large);
        const auto first_character = text.find_first_not_of(" \t\r\n");
        const auto last_character = text.find_last_not_of(" \t\r\n");
        if (first_character == std::string_view::npos || text[first_character] != '{' || text[last_character] != '}')
            return std::unexpected(capture_result::invalid_text);
        for (std::size_t index = 0; index < text.size();)
        {
            const auto first = static_cast<unsigned char>(text[index]);
            std::size_t length = 1;
            if (first < 32 && first != '\t' && first != '\r' && first != '\n')
                return std::unexpected(capture_result::invalid_text);
            if (first >= 128)
            {
                length = first >= 0xc2 && first <= 0xdf   ? 2
                         : first >= 0xe0 && first <= 0xef ? 3
                         : first >= 0xf0 && first <= 0xf4 ? 4
                                                          : 0;
                if (length == 0 || length > text.size() - index)
                    return std::unexpected(capture_result::invalid_text);
                for (std::size_t next = 1; next < length; ++next)
                    if ((static_cast<unsigned char>(text[index + next]) & 0xc0) != 0x80)
                        return std::unexpected(capture_result::invalid_text);
                const auto second = static_cast<unsigned char>(text[index + 1]);
                if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
                    (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
                    return std::unexpected(capture_result::invalid_text);
            }
            index += length;
        }
        return text.size();
    }

    std::int64_t utc_now_ns()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    struct md_logger_ai::impl
    {
        struct record
        {
            std::string received_us;
            std::string payload;
            std::size_t serialized_size;

            std::size_t size() const noexcept
            {
                return serialized_size;
            }
        };

        md_logger_ai_config config;
        nlohmann::json capture_metadata;
        std::filesystem::path run_directory;
        std::filesystem::path partial_path;
        std::filesystem::path final_path;
        mutable std::mutex mutex;
        std::condition_variable wake;
        std::deque<record> queue;
        std::vector<char> buffer;
        md_logger_ai_stats counts;
        std::optional<std::string> failure;
        bool stopping{};
        std::thread worker;
        int descriptor{-1};
        int directory_descriptor{-1};
        std::uint64_t segment{};
        std::size_t file_bytes{};
        std::chrono::steady_clock::time_point roll_at;

        explicit impl(md_logger_ai_config settings) : config(std::move(settings))
        {
            if (config.directory.empty() || config.prefix.empty() ||
                config.prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKL"
                                                "MNOPQRSTUVWXYZ0123456789_-") != std::string::npos ||
                config.max_pending_bytes < 3 || config.max_pending_records == 0 || config.max_file_bytes < 3 ||
                config.roll_interval.count() <= 0 || config.flush_interval.count() <= 0)
                throw std::invalid_argument("Invalid MD logger configuration");
            capture_metadata = nlohmann::json::parse(config.metadata_json);
            if (!capture_metadata.is_object())
                throw std::invalid_argument("MD logger metadata must be a JSON object");
            // Validate encoding before creating output or starting the worker.
            (void)capture_metadata.dump();
            std::filesystem::create_directories(config.directory);
            std::random_device random;
            bool created = false;
            for (int attempt = 0; attempt < 8 && !created; ++attempt)
            {
                const auto id = std::format("{}-{}-{:08x}{:08x}{:08x}{:08x}",
                                            config.prefix,
                                            utc_now_ns(),
                                            random(),
                                            random(),
                                            random(),
                                            random());
                run_directory = config.directory / id;
                created = std::filesystem::create_directory(run_directory);
            }
            if (!created)
                throw std::runtime_error("Could not reserve a unique capture run directory");
            directory_descriptor = ::open(run_directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (directory_descriptor < 0)
                system_failure("open capture run directory");
            try
            {
                // Persist the new run's directory entry before accepting records.
                const int parent = ::open(config.directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
                if (parent < 0)
                    system_failure("open capture parent directory");
                try
                {
                    synchronize(parent);
                }
                catch (...)
                {
                    ::close(parent);
                    throw;
                }
                if (::close(parent) != 0)
                    system_failure("close capture parent directory");
                worker = std::thread([this] { run(); });
            }
            catch (...)
            {
                ::close(directory_descriptor);
                directory_descriptor = -1;
                throw;
            }
        }

        ~impl()
        {
            stop();
            if (descriptor >= 0)
                ::close(descriptor);
            if (directory_descriptor >= 0)
                ::close(directory_descriptor);
        }

        void stop()
        {
            {
                std::lock_guard lock(mutex);
                stopping = true;
            }
            wake.notify_one();
            if (worker.joinable())
                worker.join();
        }

        void open_segment()
        {
            const auto stem = std::format("{:020}", segment++);
            const auto opened_ns = utc_now_ns();
            final_path = run_directory / (stem + ".txt");
            partial_path = run_directory / std::format("{}.{}.txt.partial", stem, opened_ns);
            const auto metadata_path = run_directory / (stem + ".meta.json");
            const auto metadata_partial = run_directory / std::format("{}.{}.meta.json.partial", stem, opened_ns);
            const nlohmann::json metadata{
                {"format",
                 {{"name", "nom.md_capture.text"},
                  {"version", 1},
                  {"encoding", "UTF-8"},
                  {"fields", {"received_us", "data"}},
                  {"field_separator", " "},
                  {"record_terminator", "\n"},
                  {"escaping", "none; CR/LF replaced by spaces"},
                  {"timestamp", {{"epoch", "Unix"}, {"unit", "microseconds"}, {"clock", "local system clock"}}}}},
                {"data_file", final_path.filename().string()},
                {"partial_file", partial_path.filename().string()},
                {"run_id", run_directory.filename().string()},
                {"opened_us", opened_ns / 1000},
                {"capture", capture_metadata}};
            const auto description = metadata.dump(2) + '\n';
            int metadata_descriptor = ::open(metadata_partial.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
            if (metadata_descriptor < 0)
                system_failure("create capture metadata");
            try
            {
                write_bytes(metadata_descriptor, description);
                synchronize(metadata_descriptor);
                if (::close(std::exchange(metadata_descriptor, -1)) != 0)
                    system_failure("close capture metadata");
                if (::renameat2(
                        AT_FDCWD, metadata_partial.c_str(), AT_FDCWD, metadata_path.c_str(), RENAME_NOREPLACE) != 0)
                    system_failure("publish capture metadata without replacement");
                synchronize(directory_descriptor);
            }
            catch (...)
            {
                if (metadata_descriptor >= 0)
                    ::close(metadata_descriptor);
                throw;
            }
            descriptor = ::open(partial_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
            if (descriptor < 0)
                system_failure("create capture segment");
            synchronize(directory_descriptor);
            file_bytes = 0;
            roll_at = std::chrono::steady_clock::now() + config.roll_interval;
        }

        void publish_segment()
        {
            if (descriptor < 0)
                return;
            synchronize(descriptor);
            const int completed = std::exchange(descriptor, -1);
            // close() errors are terminal; retrying close can close a reused descriptor
            // on Linux.
            if (::close(completed) != 0)
                system_failure("close capture segment");
            if (::renameat2(AT_FDCWD, partial_path.c_str(), AT_FDCWD, final_path.c_str(), RENAME_NOREPLACE) != 0)
                system_failure("publish capture segment without replacement");
            synchronize(directory_descriptor);
        }

        void write_batch(const std::vector<char>& bytes, std::uint64_t records)
        {
            if (bytes.empty())
                return;
            write_bytes(descriptor, bytes);
            file_bytes += bytes.size();
            std::lock_guard lock(mutex);
            counts.written_records += records;
            counts.pending_bytes -= bytes.size();
        }

        void consume(std::deque<record>& records)
        {
            auto& bytes = buffer;
            bytes.clear();
            std::uint64_t buffered_records{};
            while (!records.empty())
            {
                const auto& item = records.front();
                if (descriptor >= 0 && (file_bytes + bytes.size() + item.size() > config.max_file_bytes ||
                                        std::chrono::steady_clock::now() >= roll_at))
                {
                    write_batch(bytes, buffered_records);
                    bytes.clear();
                    buffered_records = 0;
                    publish_segment();
                }
                if (descriptor < 0)
                    open_segment();
                bytes.insert(bytes.end(), item.received_us.begin(), item.received_us.end());
                bytes.push_back(' ');
                std::ranges::transform(item.payload,
                                       std::back_inserter(bytes),
                                       [](char value) { return value == '\r' || value == '\n' ? ' ' : value; });
                bytes.push_back('\n');
                ++buffered_records;
                records.pop_front();
                if (bytes.size() >= batch_bytes)
                {
                    write_batch(bytes, buffered_records);
                    bytes.clear();
                    buffered_records = 0;
                }
            }
            write_batch(bytes, buffered_records);
        }

        void run() noexcept
        {
            try
            {
                buffer.reserve(batch_bytes);
                for (;;)
                {
                    std::deque<record> batch;
                    bool done;
                    {
                        std::unique_lock lock(mutex);
                        wake.wait_for(lock,
                                      config.flush_interval,
                                      [this] { return stopping || counts.pending_bytes >= batch_bytes; });
                        batch.swap(queue);
                        done = stopping && batch.empty();
                    }
                    if (done)
                        break;
                    consume(batch);
                    if (descriptor >= 0 && std::chrono::steady_clock::now() >= roll_at)
                        publish_segment();
                }
                publish_segment();
            }
            catch (const std::exception& error)
            {
                std::lock_guard lock(mutex);
                failure = error.what();
                stopping = true;
            }
        }
    };

    md_logger_ai::md_logger_ai(md_logger_ai_config config) : state_(std::make_unique<impl>(std::move(config)))
    {
    }
    md_logger_ai::~md_logger_ai() = default;

    capture_result md_logger_ai::append(const capture_event_ai& event)
    {
        auto& state = *state_;
        std::lock_guard lock(state.mutex);
        if (state.failure)
            return capture_result::failed;
        if (state.stopping)
            return capture_result::stopped;
        const auto timestamp = timestamp_text(
            std::chrono::duration_cast<std::chrono::microseconds>(event.received_at.time_since_epoch()).count());
        const auto overhead = timestamp.size() + 2;
        const auto limit = std::min(state.config.max_pending_bytes, state.config.max_file_bytes);
        if (overhead > limit)
            return capture_result::too_large;
        const auto payload_size = text_size(event.payload, limit - overhead);
        if (!payload_size)
            return payload_size.error();
        const auto bytes = overhead + *payload_size;
        if (state.counts.accepted_records - state.counts.written_records >= state.config.max_pending_records ||
            bytes > state.config.max_pending_bytes - state.counts.pending_bytes)
            return capture_result::full;
        state.queue.push_back({timestamp, std::string(event.payload), bytes});
        ++state.counts.accepted_records;
        state.counts.pending_bytes += bytes;
        if (state.counts.pending_bytes >= batch_bytes)
            state.wake.notify_one();
        return capture_result::accepted;
    }

    std::optional<std::string> md_logger_ai::error() const
    {
        std::lock_guard lock(state_->mutex);
        return state_->failure;
    }

    md_logger_ai_stats md_logger_ai::stats() const
    {
        std::lock_guard lock(state_->mutex);
        return state_->counts;
    }

    const std::filesystem::path& md_logger_ai::directory() const noexcept
    {
        return state_->run_directory;
    }

    std::expected<void, std::string> md_logger_ai::finish()
    {
        state_->stop();
        if (const auto failure = error())
            return std::unexpected(*failure);
        return {};
    }
} // namespace nom
