// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0
module;

#include <json_config/config.h>

export module nom.services:md_logger;

import std;
import nom.core;

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
// Load core's existing config-wrapper instantiations before declaring another config.
// Otherwise GCC 16 can emit a module that fails to import with "Bad file data".
static_assert(sizeof(nom::log_config) > 0);
#endif

export namespace nom
{
    namespace event
    {
        // Views are borrowed for log(); accepted payloads are copied before return.
        struct market_data
        {
            std::chrono::system_clock::time_point received_at;
            std::string_view payload;
        };
    } // namespace event

    struct md_logger_config
    {
        cfg_string file{};
        cfg_defaulted<cfg_uint> flush_records{16 * 1024};
        cfg_defaulted<cfg_uint> flush_ms{2000};

        std::expected<void, std::string> validate() const;
    };
    NOM_JCONFIG(md_logger_config, file, flush_records, flush_ms)

    struct md_logger_stats
    {
        // Total records added to the shared queue; excludes records still in the pending buffer.
        std::uint64_t queued_messages{};
        std::uint64_t flushed_messages{};
        std::uint64_t flushed_bytes{};
    };

    class md_logger
    {
    public:
        // Borrows config: it must remain unchanged and outlive the logger, including its worker.
        explicit md_logger(const md_logger_config& config);
        ~md_logger() noexcept;

        md_logger(const md_logger&) = delete;
        md_logger& operator=(const md_logger&) = delete;

        // One serialized producer. Copy payloads before returning; use the pending buffer if the queue is busy.
        bool log(const event::market_data& event);
        // Stop the producer first. Call from one lifecycle owner, never from the writer thread.
        bool stop();
        std::optional<std::string> error() const;
        // Read all counters under the queue mutex.
        md_logger_stats stats() const;
        const std::string& file() const noexcept;

    private:
        static constexpr std::size_t max_write_errors = 5;
        static constexpr std::chrono::milliseconds write_error_timeout{100};

        struct message
        {
            std::int64_t received_us;
            std::string content;
        };

        void move_pending_buffer_to_buffer();
        void prepare_file();
        int open_file() const;
        static void write_data(int descriptor, std::span<const char> data);
        void flush();
        void run_queue(std::stop_token stop_token) noexcept;

        const md_logger_config& config_;
        std::vector<message> pending_buffer_;
        //...
        mutable std::mutex mutex_;
        std::condition_variable_any buffer_cond_;
        std::stop_token stop_token_;
        std::optional<std::string> failure_;
        md_logger_stats stats_;
        std::vector<message> buffer_;
        std::vector<message> flush_buffer_;
        //...
        std::vector<char> bytes_;
        std::jthread worker_;
    };
} // namespace nom
