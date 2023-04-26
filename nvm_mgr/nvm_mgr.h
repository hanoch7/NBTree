#ifndef nvm_mgr_h
#define nvm_mgr_h

#include <fcntl.h>
#include <list>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <bitset>

static const char *nvm_dir = "/mnt/pmem1/";
static const char *nvm_dir1 = "/mnt/pmem0/";

namespace NVMMgr_ns {

class NVMMgr {
  public:
    static const int magic_number = 12345;
    static const int max_threads = 64;
    
    static const int PGSIZE = 256 * 1024;                     // 256K

    static const uint64_t allocate_size = 1024ULL * 1024 * PGSIZE; // 256GB
    static const uint64_t bitmap_size = allocate_size / 512; // TODO: fix 512

    size_t start_addr = 0x50000000;
    size_t bitmap_addr = start_addr + PGSIZE; // addr of bitmap
    size_t thread_local_start = bitmap_addr + bitmap_size / 8;
    size_t data_block_start = thread_local_start + 2 * PGSIZE * max_threads; // 一个PGSIZE存ti，一个PGSIZE存cicle_garbage

    static const char *get_filename() {
        static const std::string filename = std::string(nvm_dir) + "part.data";
        return filename.c_str();
    }

    static const char *get_filename1() {
        static const std::string filename = std::string(nvm_dir1) + "part.data";
        return filename.c_str();
    }

    struct Head {
        char root[4096]; // for root
        uint64_t generation_version;
        uint64_t free_bit_offset;
        int status;        // if equal to magic_number, it is reopen
        int threads;       // threads number
        uint8_t bitmap[0]; // show every page type
        // 0: free, 1: N4, 2: N16, 3: N48, 4: N256, 5: Leaf
    };

  public:
    NVMMgr(bool isother);

    ~NVMMgr();

    void *alloc_thread_info();

    void *alloc_block(int tid);

    void recovery_free_memory(int forward_thread);

    void set_bitmap(void *addr);

    void reset_bitmap(void *addr);

    // volatile metadata and rebuild when recovery
    int fd;
    bool first_created;

    // persist it as the head of nvm region
    Head *meta_data;

    // persist of bitmap
    std::bitset<bitmap_size> *bitmap;
} __attribute__((aligned(64)));

NVMMgr *get_nvm_mgr(int workerid);

bool init_nvm_mgr();

void close_nvm_mgr();
} // namespace NVMMgr_ns

#endif // nvm_mgr_h
