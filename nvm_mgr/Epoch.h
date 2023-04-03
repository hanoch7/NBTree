#pragma once

#include <chrono>
#include <iostream>
#include <thread>
#include <cstring>

namespace NVMMgr_ns {

static const int GC_NODE_COUNT_THREADHOLD = 1024; // GC threshold
static const int CICLE_SIZE = 8;
static const int MAX_THREAD = 64 * 16;

extern int* epoch;
extern bool exit_flag;

class GarbageNode {
public:
    int delete_epoch[MAX_THREAD];
    void *node_p;
    GarbageNode *next_p;

    GarbageNode(int* p_delete_epoch, void *p_node_p) {
        memcpy(delete_epoch, p_delete_epoch, sizeof(delete_epoch));
        node_p = p_node_p;
        next_p = nullptr;
    }

    GarbageNode() : delete_epoch{}, node_p{nullptr}, next_p{nullptr} {}
} __attribute__((aligned(64)));

class GCMetaData {
public:
    GarbageNode header;
    GarbageNode *last_p;
    int node_count;

    GCMetaData() {
        last_p = &header;
        node_count = 0;
    }
    ~GCMetaData() {}
} __attribute__((aligned(64)));

class Epoch_Mgr {
public:
    std::thread *thread_p;

    Epoch_Mgr() { 
        epoch = new int[MAX_THREAD];
        memset(epoch, 0, sizeof(epoch));
        exit_flag = false; 
    }

    ~Epoch_Mgr() {
        delete epoch;
        exit_flag = true;
        std::cout << "[EPOCH]\tepoch mgr exit\n";
    }

    void IncreaseEpoch(int tid) { epoch[tid * 16]++; }

    static int* GetGlobalEpoch() { 
        return epoch; 
    }

    static bool JudgeEpoch(int* gc_epoch) {
        for (int i = 0; i < MAX_THREAD; i++) {
            if (gc_epoch[i] % 2 != 0 && gc_epoch[i] < epoch[i]) {
                continue;
            }
            if (gc_epoch[i] % 2 == 0 && gc_epoch[i] <= epoch[i]) {
                continue;
            }
            return false;
        }
        return true;
    }
} __attribute__((aligned(64)));

} // namespace NVMMgr_ns
