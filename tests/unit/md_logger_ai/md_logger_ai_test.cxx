// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <nlohmann/json.hpp>
#include <unistd.h>

import std;
import nom.md_logger_ai;

using namespace std::chrono_literals;
using namespace std::string_view_literals;

static std::atomic<int> write_mode{};
static bool fail_metadata{};
static std::atomic<std::size_t> remaining_before_error{};

// GNU linker wrapping requires these symbol names; C++ identifiers follow the
// repository convention.
extern "C" ssize_t real_write(int descriptor, const void* data, size_t count) asm("__real_write");
extern "C" ssize_t wrap_write(int descriptor, const void* data, size_t count) asm("__wrap_write");
extern "C" ssize_t wrap_write(int descriptor, const void* data, size_t count)
{
    std::array<char, 4096> path;
    const auto length = ::readlink(std::format("/proc/self/fd/{}", descriptor).c_str(), path.data(), path.size());
    const auto suffix = fail_metadata ? ".meta.json.partial" : ".txt.partial";
    if (length < 0 || !std::string_view(path.data(), static_cast<std::size_t>(length)).ends_with(suffix))
        return real_write(descriptor, data, count);
    const auto mode = write_mode.load();
    if (mode == 1)
    {
        write_mode = 2;
        errno = EINTR;
        return -1;
    }
    if (mode == 2)
        return real_write(descriptor, data, std::min(count, std::size_t{3}));
    if (mode == 3 || mode == 4)
    {
        const auto remaining = remaining_before_error.load();
        if (remaining == 0)
        {
            errno = EIO;
            return mode == 4 ? 0 : -1;
        }
        const auto written = real_write(descriptor, data, std::min(count, remaining));
        if (written > 0)
            remaining_before_error -= static_cast<std::size_t>(written);
        return written;
    }
    return real_write(descriptor, data, count);
}

namespace
{
    struct temporary_directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     std::format("nom_md_test_{}_{}",
                                                 std::chrono::steady_clock::now().time_since_epoch().count(),
                                                 std::random_device{}());
        ~temporary_directory()
        {
            std::filesystem::remove_all(path);
        }
    };

    std::vector<std::filesystem::path> files(const std::filesystem::path& path, std::string_view extension)
    {
        std::vector<std::filesystem::path> result;
        for (const auto& file : std::filesystem::directory_iterator(path))
            if (file.path().extension() == extension)
                result.push_back(file.path());
        std::ranges::sort(result);
        return result;
    }

    std::string contents(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    nom::capture_event_ai event(std::string_view data)
    {
        return {std::chrono::system_clock::time_point{1234567ns}, data};
    }
} // namespace

TEST_CASE("MD logger writes readable text with microsecond timestamps and owns "
          "payloads")
{
    temporary_directory temporary;
    nom::md_logger_ai logger({.directory = temporary.path,
                              .flush_interval = 1s,
                              .metadata_json = R"({"exchange":"test","subscription":{"symbols":["BTC","ETH"]}})"});
    std::string payload = R"({"symbol":"BTC","price":"1"})";
    CHECK(logger.append(event(payload)) == nom::capture_result::accepted);
    const std::string original = payload;
    const std::string second = R"({ "note": "line\nnext\t\\literal", "px": 1.2300e+02, "dup":1, "dup":2 })";
    CHECK(logger.append(event(second)) == nom::capture_result::accepted);
    payload.assign("changed");
    REQUIRE(logger.finish());
    REQUIRE(logger.finish());
    CHECK(logger.append(event(R"({"v":"later"})")) == nom::capture_result::stopped);
    CHECK(logger.stats().accepted_records == 2);
    CHECK(logger.stats().written_records == 2);
    CHECK(logger.stats().pending_bytes == 0);
    REQUIRE(files(logger.directory(), ".txt").size() == 1);
    CHECK(files(logger.directory(), ".partial").empty());
    const auto data = contents(files(logger.directory(), ".txt").front());
    CHECK(data == "1234 " + original + "\n1234 " + second + "\n");
    REQUIRE(files(logger.directory(), ".json").size() == 1);
    const auto metadata = nlohmann::json::parse(contents(files(logger.directory(), ".json").front()));
    CHECK(metadata["format"]["version"] == 1);
    CHECK(metadata["format"]["fields"] == nlohmann::json({"received_us", "data"}));
    CHECK(metadata["format"]["timestamp"]["unit"] == "microseconds");
    CHECK(metadata["data_file"] == "00000000000000000000.txt");
    CHECK(metadata["capture"]["subscription"]["symbols"] == nlohmann::json({"BTC", "ETH"}));
}

TEST_CASE("MD logger enforces a bounded backlog and rejects oversized events")
{
    temporary_directory temporary;
    nom::md_logger_ai logger(
        {.directory = temporary.path, .max_pending_bytes = 128, .max_pending_records = 2, .flush_interval = 60s});
    CHECK(logger.append(event(R"({"v":"one"})")) == nom::capture_result::accepted);
    CHECK(logger.append(event(R"({"v":"two"})")) == nom::capture_result::accepted);
    CHECK(logger.append(event(R"({"v":"three"})")) == nom::capture_result::full);
    CHECK(logger.append(event(std::string(128, 'x'))) == nom::capture_result::too_large);
    REQUIRE(logger.finish());
    CHECK(logger.stats().written_records == 2);
}

TEST_CASE("MD logger rolls complete records and isolates concurrent capture runs")
{
    temporary_directory temporary;
    nom::md_logger_ai first({.directory = temporary.path, .max_file_bytes = 30});
    nom::md_logger_ai second({.directory = temporary.path, .max_file_bytes = 30});
    CHECK(first.directory() != second.directory());
    for (int index = 0; index < 5; ++index)
        REQUIRE(first.append(event(R"({"v":"payload"})")) == nom::capture_result::accepted);
    REQUIRE(second.append(event(R"({"v":"other"})")) == nom::capture_result::accepted);
    REQUIRE(first.finish());
    REQUIRE(second.finish());
    CHECK(files(first.directory(), ".txt").size() == 5);
    for (const auto& path : files(first.directory(), ".txt"))
        CHECK(std::filesystem::file_size(path) <= 30);
    CHECK(files(second.directory(), ".txt").size() == 1);
}

TEST_CASE("Publication collision preserves existing output and the incomplete "
          "segment")
{
    temporary_directory temporary;
    nom::md_logger_ai logger({.directory = temporary.path});
    const auto target = logger.directory() / "00000000000000000000.txt";
    {
        std::ofstream output(target);
        output << "existing archive";
    }
    REQUIRE(logger.append(event(R"({"v":"new event"})")) == nom::capture_result::accepted);
    CHECK_FALSE(logger.finish());
    CHECK(logger.error().has_value());
    CHECK(contents(target) == "existing archive");
    CHECK(files(logger.directory(), ".partial").size() == 1);
    CHECK(logger.append(event(R"({"v":"later"})")) == nom::capture_result::failed);
}

TEST_CASE("Writer errors are observable without another producer event")
{
    temporary_directory temporary;
    nom::md_logger_ai logger({.directory = temporary.path, .flush_interval = 1ms});
    std::filesystem::remove(logger.directory());
    REQUIRE(logger.append(event(R"({"v":"unwritable"})")) == nom::capture_result::accepted);
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!logger.error() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    CHECK(logger.error().has_value());
    CHECK_FALSE(logger.finish());
    CHECK(logger.stats().written_records == 0);
    CHECK(logger.stats().pending_bytes > 0);
}

TEST_CASE("Short writes and interrupted writes preserve complete records")
{
    temporary_directory temporary;
    nom::md_logger_ai logger({.directory = temporary.path});
    write_mode = 1;
    REQUIRE(logger.append(event(R"({"v":"payload"})")) == nom::capture_result::accepted);
    const auto result = logger.finish();
    write_mode = 0;
    REQUIRE(result);
    REQUIRE(files(logger.directory(), ".txt").size() == 1);
    CHECK(contents(files(logger.directory(), ".txt").front()).size() == 21);
}

TEST_CASE("A terminal or zero-progress write preserves a partial file without "
          "batch replay")
{
    for (const auto mode : {3, 4})
    {
        temporary_directory temporary;
        nom::md_logger_ai logger({.directory = temporary.path});
        remaining_before_error = 7;
        write_mode = mode;
        REQUIRE(logger.append(event(R"({"v":"payload"})")) == nom::capture_result::accepted);
        const auto result = logger.finish();
        write_mode = 0;
        CHECK_FALSE(result);
        CHECK(files(logger.directory(), ".txt").empty());
        REQUIRE(files(logger.directory(), ".partial").size() == 1);
        CHECK(std::filesystem::file_size(files(logger.directory(), ".partial").front()) == 7);
        CHECK(logger.stats().written_records == 0);
    }
}

TEST_CASE("Raw JSON bytes determine record limits and pending byte counts")
{
    temporary_directory temporary;
    nom::md_logger_ai logger({.directory = temporary.path,
                              .max_pending_bytes = 16,
                              .max_pending_records = 10,
                              .max_file_bytes = 16,
                              .flush_interval = 60s});
    CHECK(logger.append(event(R"({"v":1})")) == nom::capture_result::accepted);
    CHECK(logger.stats().pending_bytes == 13);
    CHECK(logger.append(event("{}")) == nom::capture_result::full);
    CHECK(logger.append(event(R"({"value":123})")) == nom::capture_result::too_large);
    REQUIRE(logger.finish());
    CHECK(logger.stats().pending_bytes == 0);
    CHECK(contents(files(logger.directory(), ".txt").front()) == "1234 {\"v\":1}\n");
}

TEST_CASE("Capture rejects payloads that cannot form a single JSON-object line")
{
    temporary_directory temporary;
    nom::md_logger_ai logger({.directory = temporary.path});
    for (const auto text : {""sv, "pong"sv, "[]"sv, "{\"v\":\"\0\"}"sv})
        CHECK(logger.append(event(text)) == nom::capture_result::invalid_text);
    CHECK(logger.stats().accepted_records == 0);
    REQUIRE(logger.finish());
    CHECK(std::filesystem::is_empty(logger.directory()));
}

TEST_CASE("Literal line breaks become spaces while JSON escapes stay unchanged")
{
    temporary_directory temporary;
    nom::md_logger_ai logger({.directory = temporary.path, .flush_interval = 60s});
    const std::string payload = "\r\n{\n\"note\":\"line\\nnext\\r\\n\",\r\n\"v\":1\r}\n";
    REQUIRE(logger.append(event(payload)) == nom::capture_result::accepted);
    CHECK(logger.stats().pending_bytes == payload.size() + 6);
    REQUIRE(logger.finish());
    CHECK(logger.stats().pending_bytes == 0);
    const auto data = contents(files(logger.directory(), ".txt").front());
    CHECK(data == "1234   { \"note\":\"line\\nnext\\r\\n\",  \"v\":1 } \n");
    CHECK(std::ranges::count(data, '\n') == 1);
    CHECK(data.find('\r') == std::string::npos);
}

TEST_CASE("Text capture preserves UTF-8 and rejects invalid encodings")
{
    temporary_directory temporary;
    nom::md_logger_ai logger({.directory = temporary.path});
    for (const auto value : {"\xff", "\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82"})
        CHECK(logger.append(event(std::string("{\"v\":\"") + value + "\"}")) == nom::capture_result::invalid_text);
    CHECK(logger.stats().accepted_records == 0);
    REQUIRE(logger.append(event(R"({"v":"café € 😀"})")) == nom::capture_result::accepted);
    REQUIRE(logger.finish());
    CHECK(contents(files(logger.directory(), ".txt").front()) == "1234 {\"v\":\"café € 😀\"}\n");
}

TEST_CASE("Empty capture creates no data or metadata segments")
{
    temporary_directory temporary;
    nom::md_logger_ai logger({.directory = temporary.path});
    REQUIRE(logger.finish());
    CHECK(std::filesystem::is_empty(logger.directory()));
}

TEST_CASE("Invalid metadata fails before creating capture output")
{
    temporary_directory temporary;
    CHECK_THROWS(nom::md_logger_ai({.directory = temporary.path, .metadata_json = "{"}));
    CHECK_THROWS(nom::md_logger_ai({.directory = temporary.path, .metadata_json = "[]"}));
    CHECK_FALSE(std::filesystem::exists(temporary.path));
}

TEST_CASE("Metadata failure prevents data creation and keeps recovery evidence")
{
    temporary_directory temporary;
    nom::md_logger_ai logger({.directory = temporary.path});
    remaining_before_error = 7;
    write_mode = 3;
    fail_metadata = true;
    REQUIRE(logger.append(event(R"({"v":"payload"})")) == nom::capture_result::accepted);
    const auto result = logger.finish();
    write_mode = 0;
    fail_metadata = false;
    CHECK_FALSE(result);
    CHECK(files(logger.directory(), ".txt").empty());
    CHECK(files(logger.directory(), ".json").empty());
    REQUIRE(files(logger.directory(), ".partial").size() == 1);
    CHECK(files(logger.directory(), ".partial").front().string().ends_with(".meta.json.partial"));
}

TEST_CASE("Metadata publication never replaces existing context")
{
    temporary_directory temporary;
    nom::md_logger_ai logger({.directory = temporary.path});
    const auto target = logger.directory() / "00000000000000000000.meta.json";
    {
        std::ofstream output(target);
        output << "existing context";
    }
    REQUIRE(logger.append(event(R"({"v":"payload"})")) == nom::capture_result::accepted);
    CHECK_FALSE(logger.finish());
    CHECK(contents(target) == "existing context");
    CHECK(files(logger.directory(), ".txt").empty());
    REQUIRE(files(logger.directory(), ".partial").size() == 1);
    CHECK(files(logger.directory(), ".partial").front().string().ends_with(".meta.json.partial"));
}
