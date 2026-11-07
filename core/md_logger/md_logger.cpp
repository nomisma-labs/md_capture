// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <spd_logger/logging.h>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

module nom.md_logger;

import std;
import nom.spd_logger;

namespace nom
{
    std::expected<void, std::string> md_logger_config::validate() const
    {
        if (file.empty() || std::filesystem::path(file).filename().empty() || *flush_records == 0 ||
            *flush_records > std::numeric_limits<std::size_t>::max() || *flush_ms == 0 || *flush_ms > 60000)
            return std::unexpected("MD logger requires a file, positive flush_records and flush_ms 1..60000");
        return {};
    }

    md_logger::md_logger(const md_logger_config& config) : config_(config)
    {
        if (const auto valid = config_.validate(); !valid)
            throw std::invalid_argument(valid.error());
        pending_buffer_.reserve(static_cast<std::size_t>(*config_.flush_records));
        buffer_.reserve(static_cast<std::size_t>(*config_.flush_records));
        flush_buffer_.reserve(static_cast<std::size_t>(*config_.flush_records));
        worker_ = std::jthread([this](std::stop_token token) { run_queue(std::move(token)); });
        stop_token_ = worker_.get_stop_token();
    }

    md_logger::~md_logger() noexcept
    {
        try
        {
            stop();
        }
        catch (...)
        {
            // Wake the stop-aware wait; jthread joins before the worker's state is destroyed.
            worker_.request_stop();
            // TODO(spd_logger): Add no-throw versions of LERROR and LALWAYS, then remove this guard.
            try
            {
                LERROR("MD logger cleanup failed; pending data may be lost");
            }
            catch (...)
            {
                // Diagnostic failure must not prevent the worker from joining.
            }
        }
    }

    bool md_logger::log(const capture_event& event)
    {
        message msg{std::chrono::duration_cast<std::chrono::microseconds>(event.received_at.time_since_epoch()).count(),
                    std::string(event.payload)};
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (lock.owns_lock())
        {
            if (stop_token_.stop_requested() || failure_)
                return false;
            move_pending_buffer_to_buffer();
            buffer_.emplace_back(std::move(msg));
            ++stats_.queued_messages;
            const bool notify = buffer_.size() >= *config_.flush_records;
            lock.unlock();
            if (notify)
                buffer_cond_.notify_one();
        }
        else
        {
            pending_buffer_.emplace_back(std::move(msg));
        }
        return true;
    }

    void md_logger::move_pending_buffer_to_buffer()
    {
        const size_t pending_buffer_size = pending_buffer_.size();
        if (!pending_buffer_size)
            return;
        if (buffer_.empty())
            buffer_.swap(pending_buffer_);
        else
        {
            buffer_.append_range(pending_buffer_ | std::views::as_rvalue);
            pending_buffer_.clear();
        }
        stats_.queued_messages += pending_buffer_size;
    }

    bool md_logger::stop()
    {
        {
            std::lock_guard lock(mutex_);
            // Keep the writer behind this lock until the pending buffer has been transferred.
            move_pending_buffer_to_buffer();
            worker_.request_stop();
        }
        if (worker_.joinable())
            worker_.join();
        // Joining synchronizes the worker's final status; no diagnostic string copy is needed.
        return !failure_;
    }

    std::optional<std::string> md_logger::error() const
    {
        std::lock_guard lock(mutex_);
        return failure_;
    }

    md_logger_stats md_logger::stats() const
    {
        std::lock_guard lock(mutex_);
        return stats_;
    }

    const std::string& md_logger::file() const noexcept
    {
        return config_.file;
    }

    void md_logger::prepare_file()
    {
        const auto parent = std::filesystem::path(config_.file).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
    }

    int md_logger::open_file() const
    {
        for (std::size_t errors = 0;;)
        {
            const int descriptor = ::open(config_.file.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
            if (descriptor >= 0)
                return descriptor;
            const int failure = errno;
            if (failure == EINTR)
                continue;
            if (++errors == max_write_errors)
                throw std::system_error(failure, std::generic_category(), "open MD log " + config_.file);
            std::this_thread::sleep_for(write_error_timeout);
        }
    }

    void md_logger::write_data(int descriptor, std::span<const char> data)
    {
        std::size_t errors = 0;
        while (!data.empty())
        {
            const auto written = ::write(descriptor, data.data(), data.size());
            if (written > 0)
            {
                data = data.subspan(static_cast<std::size_t>(written));
                continue;
            }
            const int failure = written == 0 ? EIO : errno;
            if (failure == EINTR)
                continue;
            if (++errors == max_write_errors)
                throw std::system_error(failure, std::generic_category(), "write MD log");
            // Keep the unwritten suffix: retrying the whole batch would duplicate its prefix.
            std::this_thread::sleep_for(write_error_timeout);
        }
    }

    void md_logger::flush()
    {
        bytes_.clear();
        for (const auto& entry : flush_buffer_)
        {
            std::array<char, 20> timestamp;
            const auto converted =
                std::to_chars(timestamp.data(), timestamp.data() + timestamp.size(), entry.received_us);
            bytes_.insert(bytes_.end(), timestamp.data(), converted.ptr);
            bytes_.push_back(' ');
            for (const char value : entry.content)
                bytes_.push_back(value == '\r' || value == '\n' ? ' ' : value);
            bytes_.push_back('\n');
        }
        const int descriptor = open_file();
        try
        {
            write_data(descriptor, bytes_);
            LINFO("Written {} bytes to file {}", bytes_.size(), config_.file);
        }
        catch (...)
        {
            ::close(descriptor);
            throw;
        }
        // Do not retry close: the descriptor may already have been released.
        if (::close(descriptor) != 0)
            throw std::system_error(errno, std::generic_category(), "close MD log");
        {
            std::lock_guard lock(mutex_);
            stats_.flushed_messages += flush_buffer_.size();
            stats_.flushed_bytes += bytes_.size();
        }
        flush_buffer_.clear();
    }

    void md_logger::run_queue(std::stop_token stop_token) noexcept
    {
        try
        {
            for (bool file_ready = false;;)
            {
                {
                    std::unique_lock lock(mutex_);
                    buffer_cond_.wait_for(lock,
                                          stop_token,
                                          std::chrono::milliseconds{static_cast<std::int64_t>(*config_.flush_ms)},
                                          [this] { return buffer_.size() >= *config_.flush_records; });
                    if (buffer_.empty())
                    {
                        if (stop_token.stop_requested())
                            break;
                        continue;
                    }
                    buffer_.swap(flush_buffer_);
                }
                // All filesystem work and serialization run outside the producer lock.
                if (!file_ready)
                {
                    prepare_file();
                    LINFO("Prepare file {} to write", config_.file);
                    file_ready = true;
                }
                flush();
            }
        }
        catch (const std::exception& failure)
        {
            std::lock_guard lock(mutex_);
            failure_ = failure.what();
        }
    }

} // namespace nom
