#ifndef thread_info_h
#define thread_info_h

#include "Epoch.h"
// #include "N.h"
// #include "N16.h"
// #include "N256.h"
// #include "N4.h"
// #include "N48.h"
// #include "LeafArray.h"
#include "pmalloc_wrap.h"
#include "tbb/concurrent_queue.h"
#include <list>

namespace NVMMgr_ns {

const static int free_list_number = 10; // from 8byte to 4K
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
    int get_freelist_size(int id) { return free_list[id].unsafe_size(); }
};

class thread_info {
  public:
    int id;
    volatile int _lock;
    thread_info *next;
    buddy_allocator *free_list;

    // epoch based GC metadata
    GCMetaData *md;

    char static_log[4032]; // 整个 thread_info的长度为 4096，所以剩下的内存
                           // 4096-64 = 4032 都可以用来做 static log。
  public:
    // interface
    thread_info();
    ~thread_info();

    void *get_static_log() { return (void *)static_log; }
    int get_thread_id() { return id; }
    inline void JoinEpoch() {
        md->last_active_epoch = Epoch_Mgr::GetGlobalEpoch();
    }

    inline void LeaveEpoch() {
        // This will make ie never be counted as active for GC
        md->last_active_epoch = static_cast<uint64_t>(-1);
    }

    /*
     * AddGarbageNode() - Adds a garbage node into the thread-local GC context
     *
     * Since the thread local GC context is only accessed by this thread, this
     * process does not require any atomicity
     *
     * This is always called by the thread owning thread local data, so we
     * do not have to worry about thread identity issues
     */

    void AddGarbageNode(void *node_p);

    /*
     * PerformGC() - This function performs GC on the current thread's garbage
     *               chain using the call back function
     *
     * Note that this function only collects for the current thread. Therefore
     * this function does not have to be atomic since its
     *
     * Note that this function should be used with thread_id, since it will
     * also be called inside the destructor - so we could not rely on
     * GetCurrentGCMetaData()
     */
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

void JoinNewEpoch();
void LeaveThisEpoch();
void MarkNodeGarbage(void *node);
uint64_t SummarizeGCEpoch();

size_t convert_power_two(size_t s);

// void *alloc_new_node_from_type(PART_ns::NTypes type);
void *alloc_new_node_from_size(size_t size);
// void free_node_from_type(uint64_t addr, PART_ns::NTypes type);
void free_node_from_size(uint64_t addr, size_t size);

struct garbage_info {
  void *addr;
  uint64_t epoch;
  garbage_info():addr(nullptr),epoch(-1){}
  garbage_info(void *a, uint64_t e):addr(a),epoch(e){}
};


class cicle_garbage
{
    public:
		int max_size;
		int front;
		int end;
		garbage_info *m_queueArr;
		
    public:
  	cicle_garbage(int maxSize, void *addr)
	{
	    max_size = maxSize;
	    front = 0;
	    end = 0;
      	m_queueArr = new (addr+sizeof(cicle_garbage)) garbage_info[max_size];
	    // memset(m_queueArr, 0 , sizeof(garbage_info)*max_size);
	}
		
	~cicle_garbage()
	{
	    delete m_queueArr;
	    m_queueArr = nullptr;
	}
 
	bool enqueue(void *addr)
	{
	    if(isfull())
	    {
		// std::cout<<"Queue is full!"<<"\n";
		return false;
	    }
 
	    m_queueArr[end] = garbage_info(addr, Epoch_Mgr::GetGlobalEpoch());
	    end = (end + 1)%max_size;
		return true;
	}
 
	void* dequeue()
	{
	    if(isempty())
	    {
		// std::cout<<"Queue is empty!"<<std::endl;
		return nullptr;
	    }
			
    	// m_queueArr[front] = NULL; //模拟出队列动作
		garbage_info *tmp = &m_queueArr[front];
	    front = (front + 1)%max_size;
		return tmp->addr;
	}

	bool isfull()
	{
	    if(max_size == -1)
	    {
		std::cout<<"Create queue error!"<<std::endl;
		return false;
	    }
	    return (end + 1)%max_size == front;
	}
 
	bool isempty()
	{
	    if(max_size == -1)
	    {
		std::cout<<"Create queue error!"<<std::endl;
		return false;
	    }
	    return end == front;
	}
 
	int size()
	{
	    return (end - front + max_size) % max_size;
	}

	// void showqueue()
	// {
	//     // if(isempty())
	//     // {
	// 	// return;
	//     // }
 
	//     // for(int i = front; i < front + size(); i++ )
	//     // {
	// 	// 	std::cout <<m_queueArr[i]<<" "<<std::endl;
	//     // }
	// }

	// void showqueuefront()
	// {
	//     	// std::cout <<m_queueArr[front]<<std::endl;
	// }
};

} // namespace NVMMgr_ns
#endif
