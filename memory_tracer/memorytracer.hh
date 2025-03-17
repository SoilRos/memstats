#ifndef MEMSTATS_MEMORYTRACER_HH
#define MEMSTATS_MEMORYTRACER_HH

#include <vector>
#include <mutex>

struct MemoryOperation {
    uintptr_t address;
    size_t size;
    bool isWrite;
    uint32_t threadId;
};

struct MallocOperation {
    void* address;
    size_t size;
};

class MemoryTracer {
public:
    static int Init(int argc, char *argv[]);
    static void Finalize(INT32 code, VOID *v);
    static const std::vector<MemoryOperation>& GetOperations();
    static const std::vector<MallocOperation>& GetMallocOperations();

    static std::vector<MallocOperation> mallocOperations;

private:
    static void RecordMemoryRead(void* ip, void* addr, uint32_t size, uint32_t tid);
    static void RecordMemoryWrite(void* ip, void* addr, uint32_t size, uint32_t tid);

    static std::vector<MemoryOperation> operations;
};

/** @brief Enable memory tracing for reads and writes of all memory blocks.
 * @details Thread-local. Do not call during static- or dynamic-initialization phase.
 * @return Whether memory tracing was enabled before to this call
 */
bool memstats_enable_memory_tracer();

/** @brief Disable memory tracing for reads and writes of all memory blocks.
 * @details Thread-local. Do not call during static- or dynamic-initialization phase.
 * @return Whether memory tracing was enabled before to this call
 */
bool memstats_disable_memory_tracer();

bool memstats_do_memory_tracing();

#endif //MEMSTATS_MEMORYTRACER_HH
