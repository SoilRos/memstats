# MemStats

Simple program that instruments the C++ operators `new` and `delete`. By the end of the program, if instrumentation is enabled, a summary of the usage of `new` is printed by default.

_**Note**: This library only instruments the C++ operators `new` and `delete`, meaning that any call made to `malloc`/`calloc` et al. will not be seen by this library._

## Environmental options

| Key                                   | Description                                              | Options                                                     | Default   |
| ------------------------------------- | -------------------------------------------------------- | ----------------------------------------------------------- | --------- |
| `MEMSTATS_INIT_ENABLE_INSTRUMENTATION`| Initial value of instrumentation enable/disable function | `true`, `1`, `false`, `0`                                  | `false`   |
| `MEMSTATS_REPORT_AT_EXIT`             | Whether to report at the exit of the program             | `true`, `1`, `false`, `0`                                  | `true`    |
| `MEMSTATS_HISTOGRAM_REPRESENTATION`   | Representation type to use on histograms                 | `box`, `shadow`, `punctuation`, `number`, `circle`, `wire`  | `box`     |
| `MEMSTATS_BINS`                       | Number of bins to draw on histograms                     | `<integer>`                                                 | `15`      |

## API

| Function                                          | Description                                                           |
| ------------------------------------------------- | --------------------------------------------------------------------- |
| `memstats_report(name)`                          | Reports statistics on `new` calls since last report. Not thread-safe. |
| `memstats_[enable\|disable]_instrumentation()`   | Enables/disables instrumentation on the calling thread. Thread-safe.  |


## CMake

```cmake
# CMakeLists.txt

# Obtain memstats
include(FetchContent)
FetchContent_Declare(
  memstats
  GIT_REPOSITORY      https://github.com/SoilRos/memstats.git
  GIT_TAG             main
)
FetchContent_MakeAvailable(memstats)

# create custom target
add_executable(example_01 example_01.cc)

# consume memstats target
target_link_libraries(example_01 PUBLIC MemStats::MemStats)
```

## Example

For any program linked against the memestats library, any call to `new` and `delete` is instrumented, for example:

```c++
// example_01.cc

#include <random>
#include <vector>

int main()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    {
        std::normal_distribution<> distrib(400, 50);
        for (int i = 0; i != 10000; ++i)
            std::vector<double> v(std::abs(distrib(gen)));
    }
    {
        std::normal_distribution<> distrib(200, 65);
        for (int i = 0; i != 10000; ++i)
            std::vector<double> v(std::abs(distrib(gen)));
    }
}
```

When the executable is run and instrumentation initialization is enabled, the program will print a report on memory used by `new`.

```log
MEMSTATS_INIT_ENABLE_INSTRUMENTATION=true MEMSTATS_BINS=75 ./example_01

------------------- MemStats default -------------------
[          ▁▁▁▂▂▃▃▃▃▃▅▅▅▆▅▄▆▆▅▅▄▅▄▃▄▃▃▃▃▂▃▃▃▃▄▄▅▅▇▇▆▇█▇█▆▆▆▄▄▃▂▂▁▁▁         ]4kB    |   45MB(19k  ) | Total
[          ▁▁▁▂▂▃▃▃▃▃▅▅▅▆▅▄▆▆▅▅▄▅▄▃▄▃▃▃▃▂▃▃▃▃▄▄▅▅▇▇▆▇█▇█▆▆▆▄▄▃▂▂▁▁▁         ]4kB    |   45MB(19k  ) | Thread 0x1fe740c00

MemStats Legend:

  [{hist}]{max} | {accum}({count}) | {pos}

• hist:   Distribution of number of 'new' allocations for a given number of bytes
• max:    Maximum allocation requested to 'new'
• accum:  Accumulated number of bytes requested
• count:  Number of total allocation requests
• pos:    Position of the measurment

MemStats Histogram Legend:

• ' ' -> [ 0.0%,  11.1%)
• '▁' -> [11.1%,  22.2%)
• '▂' -> [22.2%,  33.3%)
• '▃' -> [33.3%,  44.4%)
• '▄' -> [44.4%,  55.6%)
• '▅' -> [55.6%,  66.7%)
• '▆' -> [66.7%,  77.8%)
• '▇' -> [77.8%,  88.9%)
• '█' -> [88.9%, 100.0%]
```

Most importantly, it can be enabled and disabled in any part of the program by using the API:


```c++
// example_02.cc

#include <random>
#include <vector>

#include <memstats.hh>

int main()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    for (int rep = 1; rep != 4; ++rep) {
        // only instrument a part of the code
        memstats_enable_instrumentation();
        std::normal_distribution<> distrib(rep*100, 50);
        for (int i = 0; i != 10000; ++i)
            std::vector<double> v(std::abs(distrib(gen)));
        memstats_disable_instrumentation();
        memstats_report( ("report " + std::to_string(rep)).c_str() );
    }
    {
        // this part is not instrumented
        std::normal_distribution<> distrib(200, 65);
        for (int i = 0; i != 10000; ++i)
            std::vector<double> v(std::abs(distrib(gen)));
    }
}
```

Similarly, this will instrument and report on your code, but only for specific parts of it:

```log
MEMSTATS_BINS=50 MEMSTATS_HISTOGRAM_REPRESENTATION=shadow ./example_02

------------------- MemStats report 1 -------------------
[░░░░░░▒▒▒▓▒█▓█▓█████████▓▓▒▒▒▒▒░░░                ]2kB    |    7MB(9k   ) | Total
[░░░░░░▒▒▒▓▒█▓█▓█████████▓▓▒▒▒▒▒░░░                ]2kB    |    7MB(9k   ) | Thread 0x1fe740c00

------------------- MemStats report 2 -------------------
[               ░░░░▒▒▓▓▓██████▓▓▓▒▒▒░░░           ]2kB    |   15MB(10k  ) | Total
[               ░░░░▒▒▓▓▓██████▓▓▓▒▒▒░░░           ]2kB    |   15MB(10k  ) | Thread 0x1fe740c00

------------------- MemStats report 3 -------------------
[                      ░░░▒▓▓██████▓▓▒▒░░░         ]3kB    |   22MB(10k  ) | Total
[                      ░░░▒▓▓██████▓▓▒▒░░░         ]3kB    |   22MB(10k  ) | Thread 0x1fe740c00

MemStats Legend:

  [{hist}]{max} | {accum}({count}) | {pos}

• hist:   Distribution of number of 'new' allocations for a given number of bytes
• max:    Maximum allocation requested to 'new'
• accum:  Accumulated number of bytes requested
• count:  Number of total allocation requests
• pos:    Position of the measurment

MemStats Histogram Legend:

• ' ' -> [ 0.0%,  20.0%)
• '░' -> [20.0%,  40.0%)
• '▒' -> [40.0%,  60.0%)
• '▓' -> [60.0%,  80.0%)
• '█' -> [80.0%, 100.0%]
```
