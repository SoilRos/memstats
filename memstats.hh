#ifndef MEMSTATS_HH
#define MEMSTATS_HH

#include <string_view>

namespace MemStats {

    /** @brief Reports on instrumented statistics and flushes instrumented data.
     * @details Only performs report if any instrumentation has been collected.
     * Do not call when other threads may call 'new' and 'delete', i.e., make
     * sure to syncronize all threads before and after calling report.
     * Do not call during static- or dynamic-initialization phase.
     */
    void report(std::string_view report_name = "");

    /** @brief Enable instrumentation of 'new' and 'delete' for the calling thread.
     * @details Thread-local. Do not call during static- or dynamic-initialization phase.
     * @return Whether instrumentation was enabled before to this call
     */
    bool enable_instrumentation();

    /** @brief Disable instrumentation of 'new' and 'delete' for the calling thread.
     * @details Thread-local. Do not call during static- or dynamic-initialization phase.
     * @return Whether instrumentation was enabled before to this call
     */
    bool disable_instrumentation();
}

#endif // MEMSTATS_HH
