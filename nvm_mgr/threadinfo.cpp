#include "threadinfo.h"
#include "nvm_mgr.h"
#include "pmalloc_wrap.h"
// #include "timer.h"
#include <assert.h>
#include <iostream>
#include <list>
#include <mutex>
#include "util.h"
// #include "../include/nbtree.h"

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

size_t convert_power_two(size_t s) {
    return ti->free_list->get_power_two_size(s);
}

/*************************buddy_allocator interface**************************/

void buddy_allocator::insert_into_freelist(uint64_t addr, size_t size) {
    uint64_t curr_addr = addr;
    size_t curr_size = size;
    while (curr_size) {
        //        std::cout<<"size is "<<curr_size <<"\n";
        for (int curr_id = free_list_number - 1; curr_id >= 0; curr_id--) {
            if (curr_addr % power_two[curr_id] == 0 &&
                curr_size >= power_two[curr_id]) {
                std::cout<< "curr_addr " << curr_addr << "\t" << "curr_size " << curr_size << "\t" << "curr_id " << curr_id << "\t" << "power_two[curr_id] " << power_two[curr_id] << "\n";
                free_list[curr_id].push(curr_addr);
                curr_addr += power_two[curr_id];
                curr_size -= power_two[curr_id];
                break;
            }
        }
    }
    assert(curr_size == 0);
}

uint64_t buddy_allocator::get_addr(int id) { // 根据id从free_list中取addr
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
        // std::cout << "free_list size of "<< id << " : " << get_freelist_size(id) << "\n";
        return addr;
    // }

    // // pop successfully
    // if (free_list[id].try_pop(addr)) {
    //     return addr;
    // } else { // empty
    //     addr = get_addr(id + 1);
    //     // get a bigger page and splitAndUnlock half into free_list
    //     free_list[id].push(addr + power_two[id]);
    //     return addr;
    // }
}

// alloc size smaller than 4k
void *buddy_allocator::alloc_node(size_t size) {
    int id;
    for (int i = 0; i < free_list_number; i++) {
        if (power_two[i] >= size) {
            id = i;
            break;
        }
    }
    void *addr = (void *)get_addr(id);
    // pmb->set_bitmap(addr);
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
    assert(id < free_list_number);
    return power_two[id];
}

/*************************thread_info interface**************************/

thread_info::thread_info() {
    //    node4_free_list = new PMFreeList(pmblock);
    //    node16_free_list = new PMFreeList(pmblock);
    //    node48_free_list = new PMFreeList(pmblock);
    //    node256_free_list = new PMFreeList(pmblock);
    //    leaf_free_list = new PMFreeList(pmblock);

    free_list = new buddy_allocator(pmblock);

    md = new GCMetaData();
    _lock = 0;
    id = tid++;
}

thread_info::~thread_info() {
    //    delete node4_free_list;
    //    delete node16_free_list;
    //    delete node48_free_list;
    //    delete node256_free_list;
    //    delete leaf_free_list;

    delete free_list;
    delete md;
}

void thread_info::AddGarbageNode(void *node_p) {
    if (cg->enqueue(node_p)) {
        return;
    }
    GarbageNode *garbage_node_p =
        new GarbageNode(Epoch_Mgr::GetGlobalEpoch(), node_p);
    assert(garbage_node_p != nullptr);

    // Link this new node to the end of the linked list
    // and then update last_p
    md->last_p->next_p = garbage_node_p;
    md->last_p = garbage_node_p;
    //    PART_ns::BaseNode *n = (PART_ns::BaseNode *)node_p;
    //    std::cout << "[TEST]\tgarbage node type " << (int)(n->type) << "\n";
    // Update the counter
    md->node_count++;

    // It is possible that we could not free enough number of nodes to
    // make it less than this threshold
    // So it is important to let the epoch counter be constantly increased
    // to guarantee progress
    if (md->node_count > GC_NODE_COUNT_THREADHOLD) {
        // Use current thread's gc id to perform GC
        PerformGC();
    }

    return;
}

void thread_info::PerformGC() {
    // First of all get the minimum epoch of all active threads
    // This is the upper bound for deleted epoch in garbage node
    uint64_t min_epoch = SummarizeGCEpoch();

    // This is the pointer we use to perform GC
    // Note that we only fetch the metadata using the current thread-local id
    GarbageNode *header_p = &(md->header);
    GarbageNode *first_p = header_p->next_p;

    // Then traverse the linked list
    // Only reclaim memory when the deleted epoch < min epoch
    while (first_p != nullptr && first_p->delete_epoch < min_epoch) {
        // First unlink the current node from the linked list
        // This could set it to nullptr
        header_p->next_p = first_p->next_p;

        // Then free memory
        FreeEpochNode(first_p->node_p);

        delete first_p;
        assert(md->node_count != 0UL);
        md->node_count--;

        first_p = header_p->next_p;
    }

    // If we have freed all nodes in the linked list we should
    // reset last_p to the header
    if (first_p == nullptr) {
        md->last_p = header_p;
    }

    return;
}

void thread_info::FreeEpochNode(void *node_p) {
//     PART_ns::BaseNode *n = reinterpret_cast<PART_ns::BaseNode *>(node_p);

//     if (n->type == PART_ns::NTypes::Leaf) {
//         // reclaim leaf key
//         PART_ns::Leaf *leaf = (PART_ns::Leaf *)n;
// #ifdef KEY_INLINE
//         free_node_from_size((uint64_t)n, sizeof(PART_ns::Leaf) + leaf->key_len +
//                                              leaf->val_len);
// #else
//         free_node_from_size((uint64_t)(leaf->fkey), leaf->key_len);
//         free_node_from_size((uint64_t)(leaf->value), leaf->val_len);
//         free_node_from_type((uint64_t)n, n->type);
// #endif
//     } else {
//         // reclaim the node
//         free_node_from_type((uint64_t)n, n->type);
//     }

    free_node_from_size((uint64_t)node_p, size_t(512));
}

void *alloc_new_node_from_size(size_t size) {
// #ifdef COUNT_ALLOC
//     if (dcmm_time == nullptr)
//         dcmm_time = new cpuCycleTimer();
//     dcmm_time->start();
// #endif
    void *addr = cg->dequeue(); // TODO: size
    if (addr != nullptr) {
        return addr;
    }

    addr = ti->free_list->alloc_node(size);
    return addr;
}

void free_node_from_size(uint64_t addr, size_t size) {
    // size_t node_size = ti->free_list->get_power_two_size(size);
    // ti->free_list->insert_into_freelist(addr, node_size);
    ti->free_list->insert_into_freelist(addr, size);
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
        //        std::cout<<"PPPPP meta data addr "<<
        //        get_nvm_mgr()->meta_data<<"\n";
    }
    if (epoch_mgr == nullptr) {
        epoch_mgr = new Epoch_Mgr();

        // need to call function to create a new thread to increase epoch
        epoch_mgr->StartThread();
        std::cout << "[THREAD]\tfirst new epoch_mgr and add global epoch\n";
    }
    if (ti == nullptr) {
        if (tid == NVMMgr::max_threads) {
            std::cout << "[THREAD]\tno available threadinfo to allocate\n";
            assert(0);
        }
        NVMMgr *mgr = get_nvm_mgr();
        // std::cout << "[THREAD]\tin thread get mgr meta data addr" << mgr->meta_data << "\n";

        void *addr = mgr->alloc_thread_info();
        ti = new (addr) thread_info();
        std::cout << "[THREAD]\tthreadinfo " << ti << "\n";
        ti->next = ti_list_head;
        ti_list_head = ti;

        // persist thread info
        flush_data((void *)ti, NVMMgr::PGSIZE);

        void *cg_addr =addr+NVMMgr::PGSIZE;
        cg = new (cg_addr) cicle_garbage(10, cg_addr); // TODO size
        // std::cout << "addr of cg: "<< cg << "\n";
        // std::cout << "size of cg: "<< sizeof(cicle_garbage) << "\n";
        // std::cout << "addr of queue: "<< cg->m_queueArr << "\n";
        flush_data((void *)cg, NVMMgr::PGSIZE);

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
            assert(next);
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

void JoinNewEpoch() { ti->JoinEpoch(); }

void LeaveThisEpoch() { ti->LeaveEpoch(); }

void MarkNodeGarbage(void *node) { ti->AddGarbageNode(node); }

uint64_t SummarizeGCEpoch() {
    assert(ti_list_head);

    // Use the first metadata's epoch as min and update it on the fly
    thread_info *tmp = ti_list_head;
    uint64_t min_epoch = tmp->md->last_active_epoch;

    // This might not be executed if there is only one thread
    while (tmp->next) {
        tmp = tmp->next;
        min_epoch = std::min(min_epoch, tmp->md->last_active_epoch);
    }

    return min_epoch;
}

} // namespace NVMMgr_ns
