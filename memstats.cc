#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <new>
#include <tuple>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if __has_include(<version>)
#include <version>
#endif

#if MEMSTAT_HAVE_STACKTRACE
#include <stacktrace>
#endif

#if MEMSTAT_HAVE_TBB
#include <oneapi/tbb/concurrent_vector.h>
#endif

#if __cpp_constinit >= 201907L
#define MEMSTATS_CONSTINIT constinit
#else
#define MEMSTATS_CONSTINIT
#endif

#include "memstats.hh"

// all allocations within this library need to use malloc/free instad of new/delete
template <class T>
class MallocAllocator
{
public:
    using value_type = T;

    constexpr MallocAllocator() noexcept = default;
    template <class U>
    constexpr MallocAllocator(const MallocAllocator<U> &) noexcept {}
    ~MallocAllocator() noexcept = default;

    T *allocate(std::size_t n)
    {
        if (n > this->max_size())
            throw std::bad_alloc();

        T *ret = static_cast<T *>(std::malloc(n * sizeof(T)));
        if (!ret)
            throw std::bad_alloc();
        return ret;
    }

    void deallocate(T *p, std::size_t)
    {
        std::free(p);
    }

    std::size_t max_size() const noexcept
    {
        return std::size_t(-1) / sizeof(T);
    }

    friend bool operator==(const MallocAllocator&, const MallocAllocator&){
        return true;
    }
    friend bool operator!=(const MallocAllocator&, const MallocAllocator&){
        return false;
    }
};

struct MemStatsInfo
{
    const void *ptr = nullptr;
    std::size_t size = 0;
    std::chrono::high_resolution_clock::time_point time = {};
    std::thread::id thread = {};
#if MEMSTAT_HAVE_STACKTRACE
    std::basic_stacktrace<MallocAllocator<std::stacktrace_entry>> stacktrace;
#endif

    static void record(void *ptr, std::size_t sz = 0);
};

bool init_memstats_instrumentation_thread()
{
    if (char *ptr = std::getenv("MEMSTATS_THREAD_INSTRUMENTATION_INIT"))
    {
        if (std::strcmp(ptr, "true") == 0 or std::strcmp(ptr, "1") == 0)
            return true;
        if (std::strcmp(ptr, "false") == 0 or std::strcmp(ptr, "0") == 0)
            return false;
        std::cerr << "Option 'MEMSTATS_THREAD_INSTRUMENTATION_INIT=" << ptr << "' not known. Fallback on default 'false'\n";
    }
    return false;
}

/** NOTE: initialization order fiasco on the sight!
 * The operator 'new' and 'delete' are automatically exposed to the whole program and
 * dynamic-initializtion of other global variables may be interleaved with the ones defined here.
 * This means that another translation unit may try to use 'new' or 'delete' during its static
 * initialization/destruction phase and may end up triggering an undefined behavior on this one.
 * Therefore, we need to reason about the order of initialization of global variables here with
 * respect to global variables in other libraries. Three key points help us to get this right:
 * (i) 'new'/'delete' cannot be used during constant- and zero-initialization, but on dynamic- and automatic-initialization phases.
 * (ii) dynamic-initialization of the variables in this translation unit happen in the order they appear here.
 * (iii) Calls to std::atexit(...) happen in reverse order as they are invoked.
 */

static std::recursive_mutex memstats_lock = {};

#if MEMSTAT_HAVE_TBB
// Non-'constinit' is problematic because its initialization cannot be done as constant-initialization
// and a call to 'new' during dynamic-initialization in another library may try to write to this variable even before its initialized.
/*MEMSTATS_CONSTINIT*/ tbb::concurrent_vector<MemStatsInfo, MallocAllocator<MemStatsInfo>> memstats_events = {};
#else
// 'constinit' is good because it will be initialized before any dynamic-initialization happens
#if __cpp_lib_constexpr_vector >= 201907L
MEMSTATS_CONSTINIT
#endif
static std::vector<MemStatsInfo, MallocAllocator<MemStatsInfo>> memstats_events = {};
#endif

// Zero- and dynamic-initialization of a thread-local variable does not necessarily happen on any order related to the global ones
static thread_local bool memstats_instrumentation_thread = init_memstats_instrumentation_thread();

// We need to make absolutely sure this is constinit so that 'memstats_instrumentation_global' is const-initialized,
// otherwise threads will try to syncronize with an uninitialized variable
#if MEMSTAT_ATOMIC_CONSTEXPR
MEMSTATS_CONSTINIT static std::atomic<bool> memstats_instrumentation_global{false};
#else
#error "MemStats needs a conforming C++ standard library where 'std::atomic<bool>' can be const-initialized, i.e. its constructor is 'constexpr'!"
#endif

bool init_memstats_instrumentation_guard()
{
    bool instrument = false;
    // Note this variable is const-initialized to false. Here we change it to true and syncronize other threads during dynamic initialization
    if (char *ptr = std::getenv("MEMSTATS_ENABLE_INSTRUMENTATION"))
    {
        if (std::strcmp(ptr, "true") == 0 or std::strcmp(ptr, "1") == 0)
            instrument = true;
        else if (std::strcmp(ptr, "false") == 0 or std::strcmp(ptr, "0") == 0)
            instrument = false;
        else
            std::cerr << "Option 'MEMSTATS_ENABLE_INSTRUMENTATION=" << ptr << "' not known. Fallback on default 'false'\n";
    }
    memstats_instrumentation_global.store(instrument, std::memory_order_acquire);
    return instrument;
}

// Const-initialization (happens before dynamic-initialization) assigns 'false' to 'memstats_instrumentation_global' which is fine because no instrumentation will be done, and 'memstats_events' won't be called.
// By defining 'memstats_instrumentation_global' after 'memstats_events' we guarantee that they are initialized on that order during dynamic-initialization.
// meaning that we cannot register memory events before 'memstats_events' is initialized.
// Note that we do not want this variable to be const-initialized to 'true' before dynamic-initialization, so we make sure this gets
// dynamic-initialized in the correct order by delaying its initialization by a non-constexpr function.
static bool memstats_instrumentation_guard = init_memstats_instrumentation_guard();

bool init_memstats_at_exit()
{
    static std::once_flag report_flag;
    std::call_once(report_flag,
        []{ std::atexit([]{
            memstats_instrumentation_global.store(false, std::memory_order_release);
            bool do_report_at_exit = true;
            if (char *ptr = std::getenv("MEMSTATS_REPORT_AT_EXIT"))
            {
                if (std::strcmp(ptr, "true") == 0 or std::strcmp(ptr, "1") == 0)
                    do_report_at_exit =  true;
                else if (std::strcmp(ptr, "false") == 0 or std::strcmp(ptr, "0") == 0)
                    do_report_at_exit =  false;
                else
                    std::cerr << "Option 'MEMSTATS_REPORT_AT_EXIT=" << ptr << "' not known. Fallback on default 'true'\n";
            }
            if (do_report_at_exit)
                memstats_report("default");
        });
    });
    return true;
}

// Destruction order fiasco also hits here. If a variable destroyed during dynamic-initialization-destruction (reverse order),
// calls on 'delete' or direct accesses to 'memstats_events' may trigger an access to an already destroyed 'memstats_events'.
// Therefore, we make sure to make a 'report' before 'memstats_events' is destroyed.
static const bool memstats_at_exit_guard = init_memstats_at_exit();

/** Overview of initialization/destruction order:
 * memstats_instrumentation_global = false;                                                     // const-initialization
 * memstats_lock = {};                                                                          // dynamic-initialization
 * memstats_events = {};                                                                        // dynamic-initialization
 * init_memstats_instrumentation_guard(); -> memstats_instrumentation_global = true;            // dynamic-initialization
 * memstats_at_exit_guard = init_memstats_at_exit();                                    // dynamic-initialization
 * main();
 * memstats_instrumentation_global = false;
 * std::atexit(default_report); -> read memstats_events                                         // dynamic-initialization-destruction
 * memstats_events.~vector<MemStatsInfo, MallocAllocator<MemStatsInfo>>();                      // dynamic-initialization-destruction
 * memstats_lock.~mutex();                                                                      // dynamic-initialization-destruction
 */

// bin representation of percentage from 0% to 100%
static const std::array<const char*,4> memstats_str_precentage_punctuation{" ", ".", ":", "!"};
static const std::array<const char*,4> memstats_str_precentage_circle{" ", ".", "o", "O"};
static const std::array<const char*,5> memstats_str_precentage_shadow{" ", "░", "▒", "▓", "█"};
static const std::array<const char*,5> memstats_str_precentage_wire{" ", "-", "~", "=", "#"};
static const std::array<const char*,9> memstats_str_precentage_box{" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
static const std::array<const char*,10> memstats_str_precentage_number{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

auto memstats_str_hist_representation()
{
    if (const char *ptr = std::getenv("MEMSTATS_HISTOGRAM_REPRESENTATION"))
    {
        if (std::strcmp(ptr, "box") == 0)
            return std::make_pair(memstats_str_precentage_box.data(), memstats_str_precentage_box.size());
        if (std::strcmp(ptr, "number") == 0)
            return std::make_pair(memstats_str_precentage_number.data(), memstats_str_precentage_number.size());
        if (std::strcmp(ptr, "punctuation") == 0)
            return std::make_pair(memstats_str_precentage_punctuation.data(), memstats_str_precentage_punctuation.size());
        if (std::strcmp(ptr, "shadow") == 0)
            return std::make_pair(memstats_str_precentage_shadow.data(), memstats_str_precentage_shadow.size());
        if (std::strcmp(ptr, "wire") == 0)
            return std::make_pair(memstats_str_precentage_wire.data(), memstats_str_precentage_wire.size());
        if (std::strcmp(ptr, "circle") == 0)
            return std::make_pair(memstats_str_precentage_circle.data(), memstats_str_precentage_circle.size());
        std::cerr << "Option 'MEMSTATS_HISTOGRAM_REPRESENTATION=" << ptr << "' not known. Fallback on default 'box'\n";
    }
    return std::make_pair(memstats_str_precentage_box.data(), memstats_str_precentage_box.size());
}

unsigned short memstats_bins()
{
    if (const char *ptr = std::getenv("MEMSTATS_BINS"))
    {
        try
        {
            return std::stoi(ptr);
        }
        catch (...)
        {
            std::cerr << "Option 'MEMSTATS_BINS=" << ptr << "' not known. Fallback on default '15'\n";
        }
    }
    return 15;
}

void MemStatsInfo::record(void *ptr, std::size_t sz)
{
    auto time = std::chrono::high_resolution_clock::now();
    MemStatsInfo info;
    info.ptr = ptr;
    info.size = sz;
    info.time = time;
    info.thread = std::this_thread::get_id();
#if MEMSTAT_HAVE_STACKTRACE
    info.stacktrace = info.stacktrace.current(2);
#endif
#if !MEMSTAT_HAVE_TBB
    std::unique_lock<std::recursive_mutex> lk{memstats_lock};
#endif
    memstats_events.emplace_back(std::move(info));
}

template <class Key, class T>
using unordered_map = std::unordered_map<Key, T, std::hash<Key>, std::equal_to<Key>, MallocAllocator<std::pair<const Key, T>>>;
using string = std::basic_string<char, std::char_traits<char>, MallocAllocator<char>>;
using stringstream = std::basic_stringstream<char, std::char_traits<char>, MallocAllocator<char>>;

void print_legend()
{
    std::cout << "\nMemStats Legend:\n\n";
    std::cout << "  [{hist}]{max} | {accum}({count}) | {pos}\n\n";
    std::cout << "• hist:   Distribution of number of 'new' allocations for a given number of bytes\n";
    std::cout << "• max:    Maximum allocation requested to 'new'\n";
    std::cout << "• accum:  Accumulated number of bytes requested\n";
    std::cout << "• count:  Number of total allocation requests\n";
    std::cout << "• pos:    Position of the measurment\n";
    std::cout << "\nMemStats Histogram Legend:\n\n";
    const auto str_precentage = memstats_str_hist_representation();
    string buffer;
    double per_width = 100. / str_precentage.second;
    for (std::size_t i = 0; i != str_precentage.second; ++i)
      std::cout << "• \'" << str_precentage.first[i] << "\' -> [" << std::fixed
                << std::setw(4) << std::setprecision(1) << i * per_width
                << "%, " << std::setw(5) << (i + 1) * per_width << '%'
                << (i + 1 == str_precentage.second ? ']' : ')') << std::endl;
}

void memstats_report(const char * report_name)
{
    auto lock = std::unique_lock<std::recursive_mutex>{memstats_lock};
    if (memstats_events.size() == 0)
        return;
    std::cout << "\n------------------- MemStats " << report_name << " -------------------\n";
    struct Stats
    {
        std::size_t count{0}, size{0}, max_size{0};
        unordered_map<std::size_t, std::size_t> size_freq;
    };
    Stats global_stats;
    unordered_map<std::thread::id, Stats> thread_stats;
#if MEMSTAT_HAVE_STACKTRACE
    unordered_map<std::basic_stacktrace<MallocAllocator<std::stacktrace_entry>>, Stats> stacktrace_stats;
    unordered_map<std::stacktrace_entry, Stats> stacktrace_entry_stats;
#endif
    for (const MemStatsInfo &info : memstats_events)
    {
        auto register_stats = [&](auto &stats)
        {
            if (info.size)
                ++stats.count;
            stats.size += info.size;
            stats.max_size = std::max(stats.max_size, info.size);
            if (info.size)
                ++stats.size_freq[info.size];
        };

        register_stats(global_stats);
        register_stats(thread_stats[info.thread]);

#if MEMSTAT_HAVE_STACKTRACE
        register_stats(stacktrace_stats[info.stacktrace]);
        for (auto entry : info.stacktrace)
            register_stats(stacktrace_entry_stats[entry]);
#endif
    }
    // clean up vector
    memstats_events.clear();

    static const std::array<char, 11> metric_prefix{' ', 'k', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y', 'R', 'Q'};
    auto bytes_to_string = [&](std::size_t bytes)
    {
        stringstream stream;
        short base = std::floor(std::log2(bytes) / 10);
        if (base > metric_prefix.size())
            throw std::out_of_range{"Too many bytes to use SI prefixes"};
        stream << short(bytes / (std::pow(1024, base))) << metric_prefix[base] << 'B';
        return stream.str();
    };

    auto int_to_string = [&](std::size_t val)
    {
        stringstream stream;
        short base = std::floor(std::log10(val) / 3);
        if (base > metric_prefix.size())
            throw std::out_of_range{"Integer is too big to use SI prefixes"};
        stream << short(val / (std::pow(1000, base))) << metric_prefix[base];
        return stream.str();
    };
    const auto str_precentage = memstats_str_hist_representation();
    const auto bins = memstats_bins();
    auto format_histogram = [&](const auto &stats)
    {
        std::vector<std::size_t, MallocAllocator<std::size_t>> hist(bins, 0);
        std::size_t max_size = 0;
        for (const auto &frec : stats.size_freq)
        {
            std::size_t size = frec.first, count = frec.second;
            assert(size <= stats.max_size);
            auto bin = (bins * (size - 1)) / (stats.max_size);
            max_size = std::max(hist[bin] += count, max_size);
        }
        stringstream stream;
        stream << "[";
        for (auto size : hist) {
          const std::size_t bin_entry =
            (size * str_precentage.second) / max_size;
          // maximum value (size==max_size) will be out of range so we need to guard agains that
          stream << str_precentage.first[std::min(bin_entry, str_precentage.second - 1)];
        }
        stream << "]" << std::left<< std::setw(6) << bytes_to_string(stats.max_size);
        return stream.str();
    };

    std::cout << format_histogram(global_stats) << " | " << std::right
              << std::setw(6) << bytes_to_string(global_stats.size) << '('
              << std::left << std::setw(5) << int_to_string(global_stats.count)
              << ") | Total\n";

    for (const auto &pair : thread_stats)
      if (pair.second.size) {
        std::cout << format_histogram(pair.second) << " | " << std::right
                  << std::setw(6) << bytes_to_string(pair.second.size) << '('
                  << std::left << std::setw(5) << int_to_string(pair.second.count)
                  << ") | Thread " << pair.first << std::endl;
      }

#if MEMSTAT_HAVE_STACKTRACE
    for (auto [stacktrace_entry, stats] : stacktrace_entry_stats)
    {
      if (stats.size) {
        std::cout << format_histogram(stats) << " | " << std::right
                  << std::setw(6) << bytes_to_string(stats.size) << '('
                  << std::left << std::setw(5)
                  << int_to_string(stats.count) << ") | ";
        std::cout << stacktrace_entry << std::endl;
      }
    }
#endif
    // avoid printing legend several times, so call once at exit
    static std::once_flag legend_flag;
    std::call_once(legend_flag, []()
                    { std::atexit(print_legend); });
}

bool memstats_enable_thread_instrumentation()
{
    return std::exchange(memstats_instrumentation_thread, true);
}

bool memstats_disable_thread_instrumentation()
{
    return std::exchange(memstats_instrumentation_thread, false);
}

bool memstats_do_instrument()
{
    return memstats_instrumentation_thread and memstats_instrumentation_global.load(std::memory_order_acquire);
}

// instrumentation of new
void *operator new(std::size_t sz)
{
    if (sz == 0)
        sz = 1;
    void *ptr;
    while ((ptr = std::malloc(sz)) == nullptr)
    {
        std::new_handler handler = std::get_new_handler();
        if (handler)
            handler();
        else
            throw std::bad_alloc{};
    }
    if (memstats_do_instrument())
        MemStatsInfo::record(ptr, sz);
    return ptr;
}

// instrumentation of new
void *operator new(std::size_t sz, std::nothrow_t) noexcept
{
    try
    {
        return ::operator new(sz);
    }
    catch (...)
    {
    }
    return nullptr;
}

// instrumentation of delete
void operator delete(void *ptr) noexcept
{
    if (memstats_do_instrument())
        MemStatsInfo::record(ptr);
    std::free(ptr);
}

// instrumentation of delete
void operator delete(void *ptr, std::nothrow_t) noexcept
{
    try
    {
        return ::operator delete(ptr);
    }
    catch (...)
    {
    }
}
