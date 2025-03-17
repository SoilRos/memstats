#include "memorytracer.hh"
#include "pin.H"
#include <algorithm>
#include <array>
#include <map>
#include <iostream>
#include <unordered_map>

static bool memstats_memory_tracing = true;

bool memstats_do_memory_tracing() {
    return memstats_memory_tracing;
}

template<class T, class U = T>
T exchange(T &obj, U &&new_value) {
    T old_value = std::move(obj);
    obj = std::forward<U>(new_value);
    return old_value;
}

bool memstats_enable_memory_tracer() {
    return exchange(memstats_memory_tracing, true);
}

bool memstats_disable_memory_tracer() {
    return exchange(memstats_memory_tracing, false);
}

std::vector<MemoryOperation> MemoryTracer::operations;
std::vector<MallocOperation> MemoryTracer::mallocOperations;

VOID malloc_before(ADDRINT size) {
    if (!memstats_do_memory_tracing())
        return;

    // Record malloc operation with nullptr as address since the adress is not known at this point
    MemoryTracer::mallocOperations.push_back({nullptr, size});
}

VOID malloc_after(ADDRINT ret) {
    if (!memstats_do_memory_tracing())
        return;

    // Update the address of the last malloc operation
    MemoryTracer::mallocOperations.back().address = reinterpret_cast<void*>(ret);
}

VOID Image(IMG img, VOID *v) {
    RTN disableRtn = RTN_FindByName(img, "disable_memory_tracer");
    if (RTN_Valid(disableRtn)) {
        RTN_Open(disableRtn);
        RTN_InsertCall(disableRtn, IPOINT_BEFORE, AFUNPTR(memstats_disable_memory_tracer), IARG_END);
        RTN_Close(disableRtn);
    }

    RTN enableRtn = RTN_FindByName(img, "enable_memory_tracer");
    if (RTN_Valid(enableRtn)) {
        RTN_Open(enableRtn);
        RTN_InsertCall(enableRtn, IPOINT_BEFORE, AFUNPTR(memstats_enable_memory_tracer), IARG_END);
        RTN_Close(enableRtn);
    }

    RTN mallocRtn = RTN_FindByName(img, "malloc");
    if (RTN_Valid(mallocRtn)) {
        RTN_Open(mallocRtn);
        RTN_InsertCall(mallocRtn, IPOINT_BEFORE, AFUNPTR(malloc_before), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
        RTN_InsertCall(mallocRtn, IPOINT_AFTER, AFUNPTR(malloc_after), IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
        RTN_Close(mallocRtn);
    }
}

int MemoryTracer::Init(int argc, char *argv[]) {

    if(PIN_Init(argc,argv))
    {
        std::cout << "Error PIN_Init" << std::endl;
        return 1;
    }

    PIN_InitSymbols();
    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction([](INS ins, VOID* v) {
        if (INS_IsMemoryRead(ins)) {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemoryRead,
                           IARG_INST_PTR, IARG_MEMORYREAD_EA, IARG_MEMORYREAD_SIZE, IARG_THREAD_ID, IARG_END);
        }
        if (INS_IsMemoryWrite(ins)) {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemoryWrite,
                           IARG_INST_PTR, IARG_MEMORYWRITE_EA, IARG_MEMORYWRITE_SIZE, IARG_THREAD_ID, IARG_END);
        }
    }, nullptr);
    PIN_AddFiniFunction(Finalize, 0);
    PIN_StartProgram();

    return 0;
}

void detect_arrays_of_arrays(const std::vector<MallocOperation>& calls) {
    for (size_t i = 0; i < calls.size(); ++i) {
        const auto& base_call = calls[i];

        // Check if it is a potential pointer array allocation (small size, multiple of 8)
        if (base_call.size >= 8 && base_call.size % sizeof(void*) == 0) {
            size_t n = base_call.size / sizeof(void*);

            std::vector<void*> candidates;
            size_t consistent_size = 0;

            // Search for n subsequent allocations with similar size
            for (size_t j = i + 1; j < calls.size(); ++j) {
                const auto& sub_call = calls[j];

                if (candidates.empty()) {
                    consistent_size = sub_call.size; // expected size
                }

                if (sub_call.size == consistent_size) {
                    candidates.push_back(sub_call.address);
                } else {
                    break; // if size is not consistent, stop searching
                }

                if (candidates.size() >= n) {
                    break; // found enough candidates!
                }
            }

            if (candidates.size() >= n / 2) { // Toleranz für unvollständige Allokation
                std::cout << "Possibly an array of arrays!\n";
                std::cout << "Pointer-Array at: " << base_call.address << " (" << base_call.size << " Bytes)\n";
                std::cout << "Arrays found (" << candidates.size() << " candidates with size " << consistent_size << "):\n";
                for (auto addr : candidates) {
                    std::cout << "  -> " << addr << "\n";
                }
                std::cout << "--------------------------------------\n";
            }
        }
    }
}

void MemoryTracer::Finalize(INT32 code, VOID* v) {
    detect_arrays_of_arrays(MemoryTracer::mallocOperations);

    // find allocations that were never used
    std::map<void*, size_t> allocations;
    for (const auto& op : MemoryTracer::mallocOperations) {
        allocations[op.address] = op.size;
    }
    for (const auto& op : MemoryTracer::operations) {
        for (auto it = op.address; it < op.address + op.size; it += 8) {
            allocations.erase(reinterpret_cast<void*>(it));
        }
    }
    for (const auto& [address, size] : allocations) {
        std::cout << "Allocation at " << address << " (" << size << " bytes) was never used" << std::endl;
    }
}

const std::vector<MemoryOperation>& MemoryTracer::GetOperations() {
    return operations;
}

void MemoryTracer::RecordMemoryRead(void* ip, void* addr, uint32_t size, uint32_t tid) {
    auto address = reinterpret_cast<uintptr_t>(addr);
    if (!memstats_do_memory_tracing())
        return;

    operations.push_back({address, size, false, tid});
}

void MemoryTracer::RecordMemoryWrite(void* ip, void* addr, uint32_t size, uint32_t tid) {
    auto address = reinterpret_cast<uintptr_t>(addr);
    if (!memstats_do_memory_tracing())
        return;

    operations.push_back({address, size, true, tid});
}

int main(int argc, char *argv[]) {
    MemoryTracer::Init(argc, argv);
    return 0;
}