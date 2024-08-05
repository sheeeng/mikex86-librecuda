#ifndef LIBRECUDA_CMDQUEUE_H
#define LIBRECUDA_CMDQUEUE_H

#include <initializer_list>
#include <cstddef>
#include <vector>
#include <deque>
#include <cstdint>
#include <mutex>
#include <condition_variable>

#include "librecuda.h"
#include "librecuda_internal.h"

#include "nvidia/nvtypes.h"

typedef NvU64 NvMethod;

static inline NvMethod makeNvMethod(int subcommand, int method, int size, int typ = 2);

struct NvSignal {
public:
    NvU64 value{};
    NvU64 time_stamp{};
};

enum QueueType {
    COMPUTE, DMA
};

struct CommandQueuePage {

    /**
     * CPU mapped GPU memory for the command queue.
     * Command queue is copied to this page for submission.
     */
    NvU32 *commandQueueSpace{};

    /**
     * Pointer into commandQueuePage. Commands are mem-copied in appending fashion into the command queue page and
     * commandWritePtr is used to keep track of the current offset from the base pointer.
     */
    uint64_t commandWritePtr = 0;

};

class NvCommandQueue {
private:
    /**
     * The parent cuda context
     */
    LibreCUcontext ctx;

    /**
     * State whether the queue has been initialized.
     * @code initializeQueue() @endcode must be called before queue methods can be called
     */
    bool initialized = false;

    /**
     * Growing list of bytes that represent the commands enqueued in the queue.
     * Once built, the command buffers contents will be copied over to the memory mapped command queue page.
     *
     * Format:
     * - NvMethod (NvU32) nvMethod
     * - vararg NvU32 expected by method (tightly packed arguments) ; size is encoded in the NvMethod bit structure
     */
    std::vector<NvU32> commandBuffer{};

    /**
     * compute queue page & stack ptr
     */
    CommandQueuePage computeQueuePage{};

    /**
     * DMA (copy) queue page & stack ptr
     */
    CommandQueuePage dmaQueuePage{};

    /**
     * CPU mapped GPU buffer of NvSignals, which can be claimed and freed again
     */
    NvSignal *signalPool{};

    /**
     * Vector of free signal handles (index into signalPool)
     */
    std::deque<NvU32> freeSignals{};

    /**
     * Primary signal used for synchronization
     */
    NvSignal *timelineSignal{};

    /**
     * Incrementing counter used for synchronization.
     * Synchronization employs signals whose values have to be greater or equal to some set value.
     * As long as the condition is false, the queue will wait.
     * Thus we have a counter value to derive new "waiting targets".
     *
     * The intuition here is that the timelineCtr advances first, as the queue is built, and the signal's value advances
     * to meet the timelineCtr. If they are equal, there is no async operation pending.
     */
    NvU32 timelineCtr = 0;

public:
    explicit NvCommandQueue(LibreCUcontext ctx);

    /**
     * Must be called before the queue is usable
     * @return status
     */
    libreCudaStatus_t initializeQueue();

    libreCudaStatus_t startExecution(QueueType queueType);

    /**
     * Waits for the pending operations in the currently executing command queue to complete
     */
    libreCudaStatus_t awaitExecution();

    ~NvCommandQueue();

private:

    libreCudaStatus_t enqueue(NvMethod method, std::initializer_list<NvU32> arguments);

    libreCudaStatus_t obtainSignal(NvSignal **pSignalPtr);

    libreCudaStatus_t releaseSignal(NvSignal *signal);

    libreCudaStatus_t signalNotify(NvSignal *pSignal, NvU32 signalTarget, QueueType type);

    libreCudaStatus_t signalWait(NvSignal *pSignal, NvU32 signalTarget);

    libreCudaStatus_t submitToFifo(GPFifo &gpfifo, CommandQueuePage &page);
};

#endif //LIBRECUDA_CMDQUEUE_H