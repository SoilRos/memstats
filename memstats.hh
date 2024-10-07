#ifndef MEMSTATS_HH
#define MEMSTATS_HH

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Reports on instrumented statistics and flushes instrumented data.
 * @details Only performs report if any instrumentation has been collected.
 * Do not call when other threads may call 'new' and 'delete', i.e., make
 * sure to syncronize all threads before and after calling report.
 * Do not call during static- or dynamic-initialization phase.
 * Reporting from a detached thread is undefined behavior.
 */
void memstats_report(const char * report_name = "");

/** @brief Enable instrumentation of 'new' and 'delete' for the calling thread.
 * @details Thread-local. Do not call during static- or dynamic-initialization phase.
 * @return Whether instrumentation was enabled before to this call
 */
bool memstats_enable_thread_instrumentation();

/** @brief Disable instrumentation of 'new' and 'delete' for the calling thread.
 * @details Thread-local. Do not call during static- or dynamic-initialization phase.
 * @return Whether instrumentation was enabled before to this call
 */
bool memstats_disable_thread_instrumentation();

#ifdef __cplusplus
}
#endif

#endif // MEMSTATS_HH
