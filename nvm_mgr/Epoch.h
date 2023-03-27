#pragma once

#include <chrono>
#include <iostream>
#include <thread>
#include <cstring>

namespace NVMMgr_ns {

static const int GC_NODE_COUNT_THREADHOLD = 1024; // 当 gc node 达到 1024 时进行一次 gc

static const int GC_INTERVAL = 50000; // 50ms 进行一次 gc

static const int CICLE_SIZE = 50;

static const int MAX_THREAD = 64 * 16;

extern int* epoch;
extern bool exit_flag;

class GarbageNode {
  public:
    // The epoch that this node is unlinked
    // This do not have to be exact - just make sure it is no earlier than the
    // actual epoch it is unlinked from the data structure
    int delete_epoch[MAX_THREAD];
    void *node_p;
    GarbageNode *next_p;

    GarbageNode(int* p_delete_epoch, void *p_node_p){
            memcpy(delete_epoch, p_delete_epoch, sizeof(delete_epoch));
            node_p = p_node_p;
            next_p = nullptr;
        }

    GarbageNode() : delete_epoch{}, node_p{nullptr}, next_p{nullptr} {}
} __attribute__((aligned(64)));

class GCMetaData {
  public:
    // This is the last active epoch counter; all garbages before this counter
    // are guaranteed to be not being used by this thread
    // So if we take a global minimum of this value, that minimum could be
    // be used as the global epoch value to decide whether a garbage node could
    // be recycled
    int* last_active_epoch;

    GarbageNode header;

    // This points to the last node in the garbage node linked list
    // We always append new nodes to this pointer, and thus inside one
    // node's context these garbage nodes are always sorted, from low
    // epoch to high epoch. This facilitates memory reclaimation since we
    // just start from the lowest epoch garbage and traverse the linked list
    // until we see an epoch >= GC epoch
    GarbageNode *last_p;

    // The number of nodes inside this GC context
    // We use this as a threshold to trigger GC
    int node_count;

    GCMetaData() {
        last_active_epoch = new int[MAX_THREAD];
        memset(last_active_epoch, 0, sizeof(last_active_epoch));
        last_p = &header;
        node_count = 0;
    }
    ~GCMetaData() {}
} __attribute__((aligned(64)));

class Epoch_Mgr {
  public:
    std::thread *thread_p;

  public:
    Epoch_Mgr() { 
        epoch = new int[MAX_THREAD];
        memset(epoch,0,sizeof(epoch));
        exit_flag = false; 
    }

    ~Epoch_Mgr() {
        exit_flag = true;
        std::cout << "[EPOCH]\tepoch mgr exit\n";
    }

    void IncreaseEpoch(int tid) { epoch[tid * 16]++; }

    static int* GetGlobalEpoch() { 
        // printf("[EPOCH]\tget epoch %d\n", epoch);
        return epoch; 
    }

    static bool JudgeEpoch(int* gc_epoch) {
        for (int i = 0; i<MAX_THREAD; i++) {
            if (gc_epoch[i] % 2 != 0 && gc_epoch[i] < epoch[i]) { // 添加gc的时候为单数，则需要now大于
                continue;
            }
            if (gc_epoch[i] % 2 == 0 && gc_epoch[i] <= epoch[i]) { // 添加gc的时候为双数，则需要now大于等于
                continue;
            }
            // std::cout <<"here\n";
            return false;
        }
        return true;
    }

    /*
     * ThreadFunc() - The cleaner thread executes this every GC_INTERVAL ms
     *
     * This function exits when exit flag is set to true
     */
    // void ThreadFunc() {
    //     // While the parent is still running
    //     // We do not worry about race condition here
    //     // since even if we missed one we could always
    //     // hit the correct value on next try
    //     std::cout << "[EPOCH]\tglobal epoch thread start\n";
    //     while (exit_flag == false) {
    //         // printf("Start new epoch cycle\n");
    //         IncreaseEpoch();

    //         // Sleep for 50 ms
    //         std::chrono::milliseconds duration(GC_INTERVAL);
    //         std::this_thread::sleep_for(duration);
    //     }
    //     std::cout << "[EPOCH]\tglobal epoch thread exit\n";
    //     return;
    // }

    // /*
    //  * StartThread() - Start cleaner thread for garbage collection
    //  *
    //  * NOTE: This is not called in the constructor, and needs to be
    //  * called manually
    //  */
    // void StartThread() {
    //     thread_p = new std::thread{[this]() { this->ThreadFunc(); }};

    //     return;
    // }
} __attribute__((aligned(64)));

} // namespace NVMMgr_ns
