#ifndef thread_info_h
#define thread_info_h

#include "Epoch.h"
#include "pmalloc_wrap.h"
#include "tbb/concurrent_queue.h"
#include <list>

namespace NVMMgr_ns {

const static int free_list_number = 10; // from 8byte to 4K
const static int log_length = 32;
// maintain free memory like buddy system in linux
class buddy_allocator {
  private:
    const size_t power_two[free_list_number] = {8,   16,  32,   64,   128,
                                                256, 512, 1024, 2048, 4096};
    tbb::concurrent_queue<uint64_t> free_list[free_list_number];
    PMBlockAllocator *pmb;

  public:
    buddy_allocator(PMBlockAllocator *pmb_) : pmb(pmb_) {
        for (int i = 0; i < free_list_number; i++)
            free_list[i].clear();
    }
    ~buddy_allocator() {}
    void insert_into_freelist(uint64_t addr, size_t size);
    void *alloc_node(size_t size);
    uint64_t get_addr(int id);
    size_t get_power_two_size(size_t s);
};

struct ThreadLog {
    uint64_t old_addr;
    uint64_t new_addr1;
    uint64_t new_addr2;
};

class thread_info {
  public:
    int id;
    volatile int _lock;
    thread_info *next;
    buddy_allocator *free_list;

    // epoch based GC metadata
    GCMetaData *md;

    ThreadLog static_log[log_length];
  public:
    // interface
    thread_info();
    ~thread_info();

    void AddGarbageNode(void *node_p);

	void SetLog(void* old_addr, void* new_addr1, void* new_addr2);
	void ResetLog(void* old_addr);

    void PerformGC();
    void FreeEpochNode(void *node_p);

} __attribute__((aligned(64)));

#ifdef COUNT_ALLOC
double getdcmmalloctime();
#endif

#ifdef INSTANT_RESTART
void init();
void increase(int id);
uint64_t total(int thread_num);
uint64_t get_threadlocal_generation();
#endif

void register_threadinfo();
void unregister_threadinfo();

void *get_threadinfo();

void MarkNodeGarbage(void *node);

void SetLog(void* old_addr, void* new_addr1, void* new_addr2);
void ResetLog(void* old_addr);

void SetBitmap(void* addr);

void increaseEpoch();

// void *alloc_new_node_from_type(PART_ns::NTypes type);
void *alloc_new_node_from_size(size_t size);
// void free_node_from_type(uint64_t addr, PART_ns::NTypes type);
void free_node_from_size(uint64_t addr, size_t size);


class cicle_garbage
{
    public:
		int max_size;
		int front;
		int end;
		uint64_t m_queueArr[CICLE_SIZE];
		int **epoch_arr;
		
    public:
  	cicle_garbage()
	{
	    max_size = CICLE_SIZE;
	    front = 0;
	    end = 0;
		epoch_arr = new int*[CICLE_SIZE];
		for (int i = 0; i < CICLE_SIZE; i ++){
			epoch_arr[i] = new int[MAX_THREAD];
		}
	}
		
	~cicle_garbage()
	{
		for (int i = 0; i < CICLE_SIZE; i ++){
			delete epoch_arr[i];
		}
		delete epoch_arr;
	}
 
	bool enqueue(void *addr)
	{
	    if(isfull())
	    {
		    return false;
	    }
		memcpy(epoch_arr[end], Epoch_Mgr::GetGlobalEpoch(), sizeof(epoch_arr[end]));
	    m_queueArr[end] = (uint64_t)addr;
	    end = (end + 1)%max_size;
		return true;
	}
 
	void* dequeue()
	{
	    if(isempty())
	    {
			return nullptr;
	    }

		if (true) {
			uint64_t addr = m_queueArr[front];
	    front = (front + 1)%max_size;
			return (void*)addr;
		}
			
		return nullptr;
	}

	bool isfull()
	{
	    return (end + 1)%max_size == front;
	}
 
	bool isempty()
	{
	    return end == front;
	}
 
	int size()
	{
	    return (end - front + max_size) % max_size;
	}
};
} // namespace NVMMgr_ns
#endif
