// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <json_config/nlohmann_json.h>

#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <pthread.h>
#include <unistd.h>

import std;
import nom.md_logger;
import nom.spd_logger;

using namespace std::chrono_literals;

// Force contention only on the producer thread; the real writer mutex stays untouched.
static thread_local unsigned busy_queue_attempts{};

// Fail one lifecycle lock on the calling thread, without disturbing the writer.
static thread_local unsigned failed_queue_locks{};

extern "C" int real_mutex_lock(pthread_mutex_t* mutex) asm("__real_pthread_mutex_lock");
extern "C" int wrap_mutex_lock(pthread_mutex_t* mutex) asm("__wrap_pthread_mutex_lock");
extern "C" int wrap_mutex_lock(pthread_mutex_t* mutex)
{
    if (failed_queue_locks != 0)
    {
        --failed_queue_locks;
        return EINVAL;
    }
    return real_mutex_lock(mutex);
}

extern "C" int real_mutex_trylock(pthread_mutex_t* mutex) asm("__real_pthread_mutex_trylock");
extern "C" int wrap_mutex_trylock(pthread_mutex_t* mutex) asm("__wrap_pthread_mutex_trylock");
extern "C" int wrap_mutex_trylock(pthread_mutex_t* mutex)
{
    if (busy_queue_attempts != 0)
    {
        --busy_queue_attempts;
        return EBUSY;
    }
    return real_mutex_trylock(mutex);
}

static std::atomic<int> write_mode{};
static std::atomic<std::size_t> remaining_before_error{};
static std::atomic<int> write_errors{};
static std::atomic<bool> hold_writes{};
static std::atomic<bool> write_entered{};

extern "C" ssize_t real_write(int descriptor, const void* data, size_t count) asm("__real_write");
extern "C" ssize_t wrap_write(int descriptor, const void* data, size_t count) asm("__wrap_write");
extern "C" ssize_t wrap_write(int descriptor, const void* data, size_t count)
{
    std::array<char, 4096> path;
    const auto length = ::readlink(std::format("/proc/self/fd/{}", descriptor).c_str(), path.data(), path.size());
    if (length < 0 || !std::string_view(path.data(), static_cast<std::size_t>(length)).ends_with("/md.log"))
        return real_write(descriptor, data, count);
    write_entered = true;
    while (hold_writes.load())
        hold_writes.wait(true);
    const int mode = write_mode.load();
    if (mode == 1)
    {
        write_mode = 2;
        errno = EINTR;
        return -1;
    }
    if (mode == 2)
        return real_write(descriptor, data, std::min(count, std::size_t{3}));
    if (mode >= 3 && mode <= 5)
    {
        const auto remaining = remaining_before_error.load();
        if (remaining == 0)
        {
            ++write_errors;
            if (mode == 5)
                write_mode = 0;
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
                                     std::format("nom_md_logger_test_{}_{}",
                                                 std::chrono::steady_clock::now().time_since_epoch().count(),
                                                 std::random_device{}());
        ~temporary_directory()
        {
            std::filesystem::remove_all(path);
        }
    };

    struct write_fault
    {
        explicit write_fault(int mode, bool hold = false)
        {
            remaining_before_error = 7;
            write_errors = 0;
            write_entered = false;
            hold_writes = hold;
            write_mode = mode;
        }
        ~write_fault()
        {
            write_mode = 0;
            hold_writes = false;
            hold_writes.notify_all();
        }
    };

    nom::capture_event event(std::string_view content)
    {
        return {std::chrono::system_clock::time_point{1234567ns}, content};
    }

    std::string contents(const std::filesystem::path& file)
    {
        std::ifstream input(file, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    template <typename Predicate> bool wait_until(Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        return predicate();
    }
} // namespace

TEST_CASE("Logger owns payloads and appends the agreed textual format to one file")
{
    temporary_directory temporary;
    const auto path = temporary.path / "nested/md.log";
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream output(path);
        output << "100 {}\n";
    }
    const nom::md_logger_config config{.file = {path.string()}, .flush_ms = {60000}};
    nom::md_logger logger(config);
    std::string payload = "{\r\n\"symbol\":\"BTC\",\"note\":\"line\\nnext\"\n}";
    REQUIRE(logger.log(event(payload)));
    payload = "changed";
    REQUIRE(logger.log(event(R"({ "symbol":"ETH", "price":1.2300e+02 })")));
    REQUIRE(logger.stop());
    REQUIRE(logger.stop());
    CHECK(logger.stats().queued_messages == 2);
    CHECK_FALSE(logger.log(event("{}")));
    const auto expected = "100 {}\n1234 {  \"symbol\":\"BTC\",\"note\":\"line\\nnext\" }\n"
                          "1234 { \"symbol\":\"ETH\", \"price\":1.2300e+02 }\n";
    CHECK(contents(path) == expected);
    CHECK(logger.stats().flushed_messages == 2);
    CHECK(logger.stats().flushed_bytes == std::string_view(expected).size() - 7);
    CHECK(logger.file() == path.string());
    CHECK(std::distance(std::filesystem::directory_iterator(path.parent_path()),
                        std::filesystem::directory_iterator{}) == 1);
}

TEST_CASE("An empty logger creates no output and destruction drains accepted messages")
{
    temporary_directory temporary;
    const auto path = temporary.path / "md.log";
    {
        const nom::md_logger_config config{.file = {path.string()}};
        nom::md_logger logger(config);
    }
    CHECK_FALSE(std::filesystem::exists(temporary.path));
    {
        const nom::md_logger_config config{.file = {path.string()}, .flush_ms = {60000}};
        nom::md_logger logger(config);
        REQUIRE(logger.log(event("{}")));
    }
    CHECK(contents(path) == "1234 {}\n");
    {
        const nom::md_logger_config config{.file = {path.string()}};
        nom::md_logger logger(config);
        REQUIRE(logger.log(event("{}")));
    }
    CHECK(contents(path) == "1234 {}\n1234 {}\n");
}

TEST_CASE("Destruction requests stop and joins when ordinary shutdown throws")
{
    temporary_directory temporary;
    const auto path = temporary.path / "md.log";
    const auto diagnostic_path = temporary.path / "app.log";
    nom::init_logger(nom::main_logger, nom::log_config{.file = nom::logfile_config{.name = diagnostic_path.string()}});
    const nom::md_logger_config config{.file = {path.string()}, .flush_ms = {60000}};
    const auto started = std::chrono::steady_clock::now();
    {
        nom::md_logger logger(config);
        REQUIRE(logger.log(event("{}")));
        failed_queue_locks = 1;
    }
    const auto unused_failures = std::exchange(failed_queue_locks, 0);
    CHECK(unused_failures == 0);
    CHECK(std::chrono::steady_clock::now() - started < 3s);
    CHECK(contents(path) == "1234 {}\n");
    nom::flush_logger(nom::main_logger);
    CHECK(contents(diagnostic_path).find("MD logger cleanup failed; pending data may be lost") != std::string::npos);
    nom::init_logger(nom::main_logger);
}

TEST_CASE("Destruction drains a final pending buffer after the shared queue")
{
    temporary_directory temporary;
    const auto path = temporary.path / "md.log";
    const nom::md_logger_config config{.file = {path.string()}, .flush_ms = {60000}};
    {
        nom::md_logger logger(config);
        REQUIRE(logger.log(event("first")));
        busy_queue_attempts = 1;
        const bool accepted = logger.log(event("second"));
        const auto unused_attempts = std::exchange(busy_queue_attempts, 0);
        REQUIRE(unused_attempts == 0);
        REQUIRE(accepted);
    }
    CHECK(contents(path) == "1234 first\n1234 second\n");
}

TEST_CASE("Flush threshold and timer both make progress before stop")
{
    temporary_directory temporary;
    SECTION("record threshold")
    {
        const nom::md_logger_config config{
            .file = {(temporary.path / "md.log").string()}, .flush_records = {2}, .flush_ms = {60000}};
        nom::md_logger logger(config);
        REQUIRE(logger.log(event("{}")));
        REQUIRE(logger.log(event("{}")));
        REQUIRE(wait_until([&] { return logger.stats().flushed_messages == 2; }));
        REQUIRE(logger.stop());
    }
    SECTION("idle timer")
    {
        const nom::md_logger_config config{.file = {(temporary.path / "md.log").string()}, .flush_ms = {10}};
        nom::md_logger logger(config);
        REQUIRE(logger.log(event("{}")));
        REQUIRE(wait_until([&] { return logger.stats().flushed_messages == 1; }));
        REQUIRE(logger.stop());
    }
}

TEST_CASE("The producer makes progress while filesystem writes are blocked")
{
    temporary_directory temporary;
    const nom::md_logger_config config{.file = {(temporary.path / "md.log").string()}, .flush_records = {1}};
    nom::md_logger logger(config);
    write_fault fault(0, true);
    REQUIRE(logger.log(event(R"({"n":1})")));
    REQUIRE(wait_until([] { return write_entered.load(); }));
    // Use a separate producer so a lock regression fails with a deadline rather than hanging the test.
    std::atomic<bool> accepted{};
    std::thread producer([&] { accepted = logger.log(event(R"({"n":2})")); });
    const bool progressed = wait_until([&] { return accepted.load(); });
    hold_writes = false;
    hold_writes.notify_all();
    producer.join();
    REQUIRE(progressed);
    REQUIRE(logger.stop());
    CHECK(contents(logger.file()) == "1234 {\"n\":1}\n1234 {\"n\":2}\n");
}

TEST_CASE("A busy queue uses the pending buffer and preserves acceptance order")
{
    temporary_directory temporary;
    const nom::md_logger_config config{.file = {(temporary.path / "md.log").string()}, .flush_ms = {60000}};
    nom::md_logger logger(config);
    REQUIRE(logger.log(event("first")));
    const auto queued_before_contention = logger.stats().queued_messages;
    busy_queue_attempts = 2;
    const bool second = logger.log(event("second"));
    const bool third = logger.log(event("third"));
    const auto unused_attempts = std::exchange(busy_queue_attempts, 0);
    REQUIRE(unused_attempts == 0);
    REQUIRE(second);
    REQUIRE(third);
    CHECK(logger.stats().queued_messages == queued_before_contention);

    std::string expected = "1234 first\n1234 second\n1234 third\n";
    std::uint64_t expected_messages = 3;
    SECTION("the next successful enqueue transfers the pending buffer first")
    {
        REQUIRE(logger.log(event("fourth")));
        expected += "1234 fourth\n";
        ++expected_messages;
    }
    SECTION("stop transfers the pending buffer without another enqueue")
    {
    }
    REQUIRE(logger.stop());
    REQUIRE(logger.stop());
    CHECK_FALSE(logger.log(event("after stop")));
    CHECK(contents(logger.file()) == expected);
    CHECK(logger.stats().queued_messages == expected_messages);
    CHECK(logger.stats().flushed_messages == expected_messages);
}

TEST_CASE("Interrupted and short writes preserve exact output")
{
    temporary_directory temporary;
    const nom::md_logger_config config{.file = {(temporary.path / "md.log").string()}};
    nom::md_logger logger(config);
    write_fault fault(1);
    REQUIRE(logger.log(event(R"({"v":"payload"})")));
    REQUIRE(logger.stop());
    CHECK(contents(logger.file()) == "1234 {\"v\":\"payload\"}\n");
}

TEST_CASE("A recoverable write failure resumes at the unwritten suffix")
{
    temporary_directory temporary;
    const nom::md_logger_config config{.file = {(temporary.path / "md.log").string()}};
    nom::md_logger logger(config);
    write_fault fault(5);
    REQUIRE(logger.log(event(R"({"v":"payload"})")));
    REQUIRE(logger.stop());
    CHECK(write_errors == 1);
    CHECK(contents(logger.file()) == "1234 {\"v\":\"payload\"}\n");
}

TEST_CASE("Persistent and zero-progress writes exhaust retries without duplicating the prefix")
{
    for (const int mode : {3, 4})
    {
        temporary_directory temporary;
        const nom::md_logger_config config{.file = {(temporary.path / "md.log").string()}, .flush_records = {1}};
        nom::md_logger logger(config);
        write_fault fault(mode);
        REQUIRE(logger.log(event(R"({"v":"payload"})")));
        REQUIRE(wait_until([&] { return logger.error().has_value(); }));
        CHECK_FALSE(logger.stop());
        CHECK_FALSE(logger.stop());
        CHECK_FALSE(logger.log(event("{}")));
        CHECK(write_errors == 5);
        CHECK(contents(logger.file()) == "1234 {\"");
        CHECK(logger.stats().flushed_messages == 0);
        CHECK(logger.stats().queued_messages == 1);
    }
}

TEST_CASE("Open failures are observable during feed silence")
{
    temporary_directory temporary;
    const auto path = temporary.path / "md.log";
    std::filesystem::create_directories(path);
    const nom::md_logger_config config{.file = {path.string()}, .flush_records = {1}};
    nom::md_logger logger(config);
    REQUIRE(logger.log(event("{}")));
    REQUIRE(wait_until([&] { return logger.error().has_value(); }));
    CHECK_FALSE(logger.stop());
    CHECK(logger.error()->find("open MD log") != std::string::npos);
}

TEST_CASE("Invalid file and flush settings fail before creating output")
{
    temporary_directory temporary;
    const auto path = temporary.path / "md.log";
    const nom::md_logger_config missing_file{};
    const nom::md_logger_config zero_records{.file = {path.string()}, .flush_records = {0}};
    const nom::md_logger_config zero_period{.file = {path.string()}, .flush_ms = {0}};
    CHECK_THROWS_AS(nom::md_logger{missing_file}, std::invalid_argument);
    CHECK_THROWS_AS(nom::md_logger{zero_records}, std::invalid_argument);
    CHECK_THROWS_AS(nom::md_logger{zero_period}, std::invalid_argument);
    CHECK_FALSE(std::filesystem::exists(temporary.path));
}

TEST_CASE("MD logger exports its JSON config mapping", "[md_logger]")
{
    nom::md_logger_config config{.file = {"capture.txt"}};
    const nlohmann::json partial{{"file", "capture.txt"}, {"flush_records", 8}, {"flush_ms", nullptr}};
    partial.get_to(config);
    const nlohmann::json expected{{"file", "capture.txt"}, {"flush_records", 8}, {"flush_ms", 200}};
    REQUIRE(nlohmann::json(config) == expected);
    REQUIRE(config.validate().has_value());
}
