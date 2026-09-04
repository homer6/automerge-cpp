#include <automerge-cpp/automerge.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>

#include <psapi.h>
#endif

namespace am = automerge_cpp;

namespace {

using Clock = std::chrono::steady_clock;

#if defined(__APPLE__)
auto read_local_memory(task_t, vm_address_t address, vm_size_t, void** local_memory)
    -> kern_return_t {
    *local_memory = reinterpret_cast<void*>(address);
    return KERN_SUCCESS;
}
#endif

auto read_bytes(const std::filesystem::path& path) -> std::vector<std::byte> {
    auto input = std::ifstream{path, std::ios::binary};
    const auto chars = std::vector<char>{std::istreambuf_iterator<char>{input}, {}};
    auto bytes = std::vector<std::byte>{};
    bytes.reserve(chars.size());
    std::ranges::transform(chars, std::back_inserter(bytes), [](const char value) {
        return static_cast<std::byte>(value);
    });
    return bytes;
}

void write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    auto output = std::ofstream{path, std::ios::binary};
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

auto resident_bytes() -> std::size_t {
#if defined(__APPLE__)
    auto info = mach_task_basic_info_data_t{};
    auto count = mach_msg_type_number_t{MACH_TASK_BASIC_INFO_COUNT};
    const auto result = task_info(
        mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
    return result == KERN_SUCCESS ? static_cast<std::size_t>(info.resident_size) : 0;
#elif defined(__linux__)
    auto input = std::ifstream{"/proc/self/statm"};
    auto total_pages = std::size_t{0};
    auto resident_pages = std::size_t{0};
    input >> total_pages >> resident_pages;
    return resident_pages * static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#elif defined(_WIN32)
    auto counters = PROCESS_MEMORY_COUNTERS{};
    const auto result =
        GetProcessMemoryInfo(GetCurrentProcess(), &counters, static_cast<DWORD>(sizeof(counters)));
    return result != 0 ? static_cast<std::size_t>(counters.WorkingSetSize) : 0;
#else
    return 0;
#endif
}

auto heap_bytes() -> std::size_t {
#if defined(__APPLE__)
    auto zones = static_cast<vm_address_t*>(nullptr);
    auto zone_count = 0u;
    const auto result =
        malloc_get_all_zones(mach_task_self(), read_local_memory, &zones, &zone_count);
    if (result != KERN_SUCCESS) {
        return 0;
    }
    auto in_use = std::size_t{0};
    for (auto index = 0u; index < zone_count; ++index) {
        auto statistics = malloc_statistics_t{};
        malloc_zone_statistics(reinterpret_cast<malloc_zone_t*>(zones[index]), &statistics);
        in_use += statistics.size_in_use;
    }
    return in_use;
#elif defined(__linux__)
    return static_cast<std::size_t>(mallinfo2().uordblks);
#else
    return resident_bytes();
#endif
}

auto make_document(std::size_t change_count,
                   std::vector<std::vector<am::ChangeHash>>* recorded_heads = nullptr)
    -> am::Document {
    auto document = am::Document{1u};
    auto actor_bytes = std::array<std::byte, am::ActorId::size>{};
    actor_bytes.fill(std::byte{0x42});
    document.set_actor_id(am::ActorId{actor_bytes});
    if (recorded_heads != nullptr) {
        recorded_heads->reserve(change_count);
    }
    for (auto index = std::size_t{0}; index < change_count; ++index) {
        document.transact([&](am::Transaction& transaction) {
            transaction.put(am::root, "value", static_cast<std::int64_t>(index));
        });
        if (recorded_heads != nullptr) {
            recorded_heads->push_back(document.get_heads());
        }
    }
    return document;
}

auto generate_files(std::size_t change_count,
                    const std::filesystem::path& full_path,
                    const std::filesystem::path& compact_path) -> int {
    auto document = make_document(change_count);
    const auto full = document.save();
    const auto compact = document.save_compact();
    write_bytes(full_path, full);
    write_bytes(compact_path, compact);
    std::cout << "changes=" << change_count << " full_bytes=" << full.size()
              << " compact_bytes=" << compact.size() << '\n';
    return 0;
}

auto measure_load(std::string_view mode, const std::filesystem::path& path) -> int {
    if (mode != "full" && mode != "shallow") {
        return 1;
    }
    const auto bytes = read_bytes(path);
    const auto heap_before = heap_bytes();
    const auto started = Clock::now();
    auto document = mode == "full" ? am::Document::load(bytes) : am::Document::load_shallow(bytes);
    const auto elapsed = Clock::now() - started;
    const auto heap_after = heap_bytes();
    if (!document || document->get<std::int64_t>(am::root, "value") == std::nullopt) {
        return 2;
    }
    const auto milliseconds = std::chrono::duration<double, std::milli>{elapsed}.count();
    const auto heap_delta = heap_after >= heap_before ? heap_after - heap_before : 0;
    std::cout << "mode=" << mode << " load_ms=" << milliseconds << " heap_bytes=" << heap_after
              << " heap_delta_bytes=" << heap_delta << '\n';
    return 0;
}

auto measure_get_at(std::size_t change_count) -> int {
    auto heads = std::vector<std::vector<am::ChangeHash>>{};
    const auto document = make_document(change_count, &heads);
    const auto first_index = change_count - 2;
    const auto second_index = change_count - 1;
    constexpr auto iterations = std::size_t{20};
    auto uncached_nanoseconds = std::chrono::nanoseconds{0};
    auto cached_nanoseconds = std::chrono::nanoseconds{0};

    for (auto iteration = std::size_t{0}; iteration < iterations; ++iteration) {
        auto sample = document;
        const auto started = Clock::now();
        const auto value = sample.get_at(am::root, "value", heads[first_index]);
        uncached_nanoseconds += Clock::now() - started;
        if (value == std::nullopt) {
            return 2;
        }
    }

    auto warmed = document;
    if (warmed.get_at(am::root, "value", heads[first_index]) == std::nullopt) {
        return 2;
    }
    for (auto iteration = std::size_t{0}; iteration < iterations; ++iteration) {
        auto sample = warmed;
        const auto started = Clock::now();
        const auto value = sample.get_at(am::root, "value", heads[second_index]);
        cached_nanoseconds += Clock::now() - started;
        if (value == std::nullopt) {
            return 2;
        }
    }

    const auto average_microseconds = [](std::chrono::nanoseconds total) {
        return std::chrono::duration<double, std::micro>{total}.count() /
               static_cast<double>(iterations);
    };
    std::cout << "changes=" << change_count
              << " uncached_get_at_us=" << average_microseconds(uncached_nanoseconds)
              << " cached_nearby_get_at_us=" << average_microseconds(cached_nanoseconds) << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 5 && std::string_view{argv[1]} == "generate") {
        return generate_files(static_cast<std::size_t>(std::stoull(argv[2])), argv[3], argv[4]);
    }
    if (argc == 4 && std::string_view{argv[1]} == "load") {
        return measure_load(argv[2], argv[3]);
    }
    if (argc == 3 && std::string_view{argv[1]} == "get-at") {
        return measure_get_at(static_cast<std::size_t>(std::stoull(argv[2])));
    }
    std::cerr << "usage: history_compaction_measure generate CHANGES FULL COMPACT\n"
                 "       history_compaction_measure load full|shallow PATH\n"
                 "       history_compaction_measure get-at CHANGES\n";
    return 1;
}
