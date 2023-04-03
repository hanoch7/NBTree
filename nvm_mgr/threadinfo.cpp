#include "threadinfo.h"
#include "nvm_mgr.h"
#include "pmalloc_wrap.h"
// #include "timer.h"
#include <assert.h>
#include <iostream>
#include <list>
#include <mutex>
#include "util.h"

namespace NVMMgr_ns {

// global block allocator
PMBlockAllocator *pmblock = nullptr;

// global threadinfo lock to protect alloc thread info
std::mutex ti_lock;

// global threadinfo list head
thread_info *ti_list_head = nullptr;

// thread local info
__thread thread_info *ti = nullptr; // 每个线程一个实例

// thread local ciclequeue
__thread cicle_garbage *cg = nullptr;

// global thread id
int tid = 0;

// global Epoch_Mgr
Epoch_Mgr *epoch_mgr = nullptr;

#ifdef COUNT_ALLOC
__thread cpuCycleTimer *dcmm_time = nullptr;
double getdcmmalloctime() { return dcmm_time->duration(); }
#endif

#ifdef INSTANT_RESTART
uint64_t thread_result[40][8];
void init() { memset(thread_result, 0, sizeof(thread_result)); }
void increase(int id) { thread_result[id][0]++; }
uint64_t total(int thread_num) {
    uint64_t ans = 0;
    for (int i = 0; i < thread_num; i++) {
        ans += thread_result[i][0];
    }
    return ans;
}

__thread uint64_t thread_generation = 0;
uint64_t get_threadlocal_generation() { return thread_generation; }
#endif

/*************************buddy_allocator interface**************************/

void buddy_allocator::insert_into_freelist(uint64_t addr, size_t size) {
    uint64_t curr_addr = addr;
    size_t curr_size = size;
    while (curr_size) {
        //        std::cout<<"size is "<<curr_size <<"\n";
        for (int curr_id = free_list_number - 1; curr_id >= 0; curr_id--) {
            if (curr_addr % power_two[curr_id] == 0 &&
                curr_size >= power_two[curr_id]) {
                free_list[curr_id].push(curr_addr);
                pmb->reset_bitmap((void*)curr_addr);
                curr_addr += power_two[curr_id];
                curr_size -= power_two[curr_id];
                break;
            }
        }
    }
}

uint64_t buddy_allocator::get_addr(int id) { // 根据id从free_list中取addr //
    uint64_t addr;
    // if (id == free_list_number - 4) { //TODO 目前只有512
        if (!free_list[id].try_pop(addr)) {
            // empty, allocate block from nvm_mgr
            thread_info *ti = (thread_info *)get_threadinfo();
            addr = (uint64_t)pmb->alloc_block(ti->id);
            // std::cout << "get addr" << addr << "\n";
            for (int i = power_two[id]; i < NVMMgr::PGSIZE;
                 i += power_two[id]) {
                free_list[id].push(addr + (uint64_t)i);
            }
        }
        return addr;
}

// alloc size smaller than 4k
void *buddy_allocator::alloc_node(size_t size) { // size = 512
    int id;
    for (int i = 0; i < free_list_number; i++) {
        if (power_two[i] >= size) {
            id = i;
            break;
        }
    }
    void *addr = (void *)get_addr(id);
    pmb->set_bitmap(addr);
    return addr;
}

size_t buddy_allocator::get_power_two_size(size_t s) {
    int id = free_list_number;
    for (int i = 0; i < free_list_number; i++) {
        if (power_two[i] >= s) {
            id = i;
            break;
        }
    }
    return power_two[id];
}

/*************************thread_info interface**************************/

thread_info::thread_info() {
    free_list = new buddy_allocator(pmblock);

    md = new GCMetaData();
    _lock = 0;
    id = tid++;
}

thread_info::~thread_info() {
    delete free_list;
    delete md;
}

void thread_info::AddGarbageNode(void *node_p) {
    if (cg->enqueue(node_p)) {
        return;
    }
    GarbageNode *garbage_node_p =
        new GarbageNode(Epoch_Mgr::GetGlobalEpoch(), node_p);

    md->last_p->next_p = garbage_node_p;
    md->last_p = garbage_node_p;
    md->node_count++;

    if (md->node_count > GC_NODE_COUNT_THREADHOLD) {
        PerformGC();
    }

    return;
}

void thread_info::PerformGC() {
    GarbageNode *header_p = &(md->header);
    GarbageNode *first_p = header_p->next_p;

    while (first_p != nullptr && Epoch_Mgr::JudgeEpoch(first_p->delete_epoch)) {
        header_p->next_p = first_p->next_p;

        FreeEpochNode(first_p->node_p);

        delete first_p;
        md->node_count--;

        first_p = header_p->next_p;
    }

    if (first_p == nullptr) {
        md->last_p = header_p;
    }

    return;
}

void thread_info::FreeEpochNode(void *node_p) {
    free_node_from_size((uint64_t)node_p, size_t(512));
}

void *alloc_new_node_from_size(size_t size) {
    void *addr = cg->dequeue(); // TODO: size
    if (addr != nullptr) {
        return addr;
    }

    addr = ti->free_list->alloc_node(size);
    return addr;
}

void free_node_from_size(uint64_t addr, size_t size) {
    ti->free_list->insert_into_freelist(addr, size);
}

void increaseEpoch() {
    epoch_mgr->IncreaseEpoch(ti->id);
}

void register_threadinfo() {
#ifdef INSTANT_RESTART
    NVMMgr *mgr = get_nvm_mgr();
    thread_generation = mgr->get_generation_version();
#endif
    std::lock_guard<std::mutex> lock_guard(ti_lock);

    printf("[NVM MGR]\tregister_threadinfo\n");
    if (pmblock == nullptr) {
        pmblock = new PMBlockAllocator(get_nvm_mgr());
        std::cout << "[THREAD]\tfirst new pmblock\n";
    }
    if (epoch_mgr == nullptr) {
        epoch_mgr = new Epoch_Mgr();

        std::cout << "[THREAD]\tfirst new epoch_mgr and add global epoch\n";
    }
    if (ti == nullptr) {
        if (tid == NVMMgr::max_threads) {
            std::cout << "[THREAD]\tno available threadinfo to allocate\n";
            assert(0);
        }
        NVMMgr *mgr = get_nvm_mgr();

        void *addr = mgr->alloc_thread_info();
        ti = new (addr) thread_info();
        std::cout << "[THREAD]\tthreadinfo " << ti << "\n";
        ti->next = ti_list_head;
        ti_list_head = ti;


        cg = new ((void*)((uint64_t)addr+NVMMgr::PGSIZE)) cicle_garbage();

        std::cout << "[THREAD]\talloc thread info " << ti->id << "\n";
    }

}

void unregister_threadinfo() {
    std::lock_guard<std::mutex> lock_guard(ti_lock);
    thread_info *cti = ti_list_head;
    if (cti == ti) {
        ti_list_head = cti->next;
    } else {
        thread_info *next = cti->next;
        while (true) {
            if (next == ti) {
                cti->next = next->next;
                break;
            }
            cti = next;
            next = next->next;
        }
    }
    std::cout << "[THREAD]\tunregister thread\n";
    // delete ti;
    ti = nullptr;
    cg = nullptr;
    if (ti_list_head == nullptr) {
        // reset all, only use for gtest
        delete epoch_mgr;
        epoch_mgr = nullptr;
        delete pmblock;
        pmblock = nullptr;
        tid = 0;
    }
}

void *get_threadinfo() { return (void *)ti; }

void MarkNodeGarbage(void *node) { ti->AddGarbageNode(node); }
} // namespace NVMMgr_ns
