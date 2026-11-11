// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

export module nom.md_logger_ai;

import std;

export namespace nom
{
    // Views are borrowed for append(); accepted payloads are copied before return.
    struct capture_event_ai
    {
        std::chrono::system_clock::time_point received_at;
        std::string_view payload;
    };

    struct md_logger_ai_config
    {
        std::filesystem::path directory;
        std::string prefix{"capture"};
        std::size_t max_pending_bytes{64 * 1024 * 1024};
        std::size_t max_pending_records{65536};
        std::size_t max_file_bytes{256 * 1024 * 1024};
        std::chrono::seconds roll_interval{300};
        std::chrono::milliseconds flush_interval{100};
        // Application context written once per segment, separate from the data
        // records.
        std::string metadata_json{"{}"};
    };

    enum class capture_result
    {
        accepted,
        full,
        too_large,
        invalid_text,
        stopped,
        failed
    };

    struct md_logger_ai_stats
    {
        std::uint64_t accepted_records{};
        std::uint64_t written_records{};
        std::size_t pending_bytes{};
    };

    // One ordered stream, one worker. No symbol parsing, filtering or file
    // selection per symbol.
    class md_logger_ai
    {
    public:
        explicit md_logger_ai(md_logger_ai_config config);
        ~md_logger_ai();
        md_logger_ai(const md_logger_ai&) = delete;
        md_logger_ai& operator=(const md_logger_ai&) = delete;

        capture_result append(const capture_event_ai& event);
        std::optional<std::string> error() const;
        md_logger_ai_stats stats() const;
        const std::filesystem::path& directory() const noexcept;
        // Stop producers first. Drain, synchronize and publish; report any
        // worker/storage error.
        std::expected<void, std::string> finish();

    private:
        struct impl;
        std::unique_ptr<impl> state_;
    };
} // namespace nom
