#include "nvm_mgr.h"
#include "threadinfo.h"
#include <cassert>
#include <iostream>
#include <math.h>
#include <mutex>
#include <stdio.h>
#include <vector>
#include "util.h"

namespace NVMMgr_ns {

// global nvm manager
NVMMgr *nvm_mgr = NULL;
NVMMgr *nvm_mgr1 = NULL;
std::mutex _mtx;

int create_file(const char *file_name, uint64_t file_size) {
    printf("[NVM MGR]\tfile name: %s\n",file_name);
    std::ofstream fout(file_name);
    if (fout) {
        fout.close();
        int result = truncate(file_name, file_size);
        if (result != 0) {
            printf("[NVM MGR]\ttruncate new file failed\n");
            exit(1);
        }
    } else {
        printf("[NVM MGR]\tcreate new file failed\n");
        exit(1);
    }

    return 0;
}

NVMMgr::NVMMgr(bool isother = false) {
    // access 的返回结果， 0: 存在， 1: 不存在
    if (!isother) {
    int initial = access(get_filename(), F_OK);
    printf("[NVM MGR]\tinitial: %d\n", initial);
    first_created = false;

    if (initial) {
        int result = create_file(get_filename(), allocate_size);
        if (result != 0) {
            printf("[NVM MGR]\tcreate file failed when initalizing\n");
            exit(1);
        }
        first_created = true;
        printf("[NVM MGR]\tcreate file success.\n");
    }

    // open file
    fd = open(get_filename(), O_RDWR);
    if (fd < 0) {
        printf("[NVM MGR]\tfailed to open nvm file\n");
        exit(-1);
    }
    if (ftruncate(fd, allocate_size) < 0) {
        printf("[NVM MGR]\tfailed to truncate file\n");
        exit(-1);
    }
    printf("[NVM MGR]\topen file %s success.\n", get_filename());

    start_addr = 0x50000000;
    bitmap_addr = start_addr + PGSIZE; // addr of bitmap
    thread_local_start = bitmap_addr + bitmap_size / 8;
    data_block_start = thread_local_start + 2 * PGSIZE * max_threads; // 一个PGSIZE存ti，一个PGSIZE存cicle_garbage
    // mmap
    void *addr = mmap((void *)start_addr, allocate_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);

    memset((void*)start_addr, 0, PGSIZE*1024UL*16);
    if (addr != (void *)start_addr) {
        printf("[NVM MGR]\tmmap failed %p \n", addr);
        exit(0);
    }
    printf("[NVM MGR]\tmmap successfully\n");

    // initialize meta data
    meta_data = static_cast<Head *>(addr);
    if (initial) {
        meta_data->status = magic_number;
        meta_data->threads = 0;
        meta_data->free_bit_offset = 0;
        meta_data->generation_version = 0;

        bitmap = new ((void *)bitmap_addr) std::bitset<bitmap_size>;

        printf("[NVM MGR]\tinitialize nvm file's head\n");
    } else {
        meta_data->generation_version++;
        printf("nvm mgr restart, the free offset is %ld, generation version "
               "is %ld\n",
               meta_data->free_bit_offset, meta_data->generation_version);
    }

    // initialize bitmap
    if (initial) {
    } else {
        // TODO
    }
    // delete tmp_bitmap;
    } else {
    int initial = access(get_filename1(), F_OK);
    printf("[NVM MGR]\tinitial: %d\n", initial);
    first_created = false;

    if (initial) {
        int result = create_file(get_filename1(), allocate_size);
        if (result != 0) {
            printf("[NVM MGR]\tcreate file failed when initalizing\n");
            exit(1);
        }
        first_created = true;
        printf("[NVM MGR]\tcreate file success.\n");
    }

    // open file
    fd = open(get_filename1(), O_RDWR);
    if (fd < 0) {
        printf("[NVM MGR]\tfailed to open nvm file\n");
        exit(-1);
    }
    if (ftruncate(fd, allocate_size) < 0) {
        printf("[NVM MGR]\tfailed to truncate file\n");
        exit(-1);
    }
    printf("[NVM MGR]\topen file %s success.\n", get_filename1());

    start_addr = 0x50000000+allocate_size;
    bitmap_addr = start_addr + PGSIZE; // addr of bitmap
    thread_local_start = bitmap_addr + bitmap_size / 8;
    data_block_start = thread_local_start + 2 * PGSIZE * max_threads; // 一个PGSIZE存ti，一个PGSIZE存cicle_garbage
    // mmap
    void *addr = mmap((void *)start_addr, allocate_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);

    memset((void*)start_addr, 0, PGSIZE*1024UL*16);
    if (addr != (void *)start_addr) {
        printf("[NVM MGR]\tmmap failed %p \n", addr);
        exit(0);
    }
    printf("[NVM MGR]\tmmap successfully\n");

    // initialize meta data
    meta_data = static_cast<Head *>(addr);
    if (initial) {
        meta_data->status = magic_number;
        meta_data->threads = 0;
        meta_data->free_bit_offset = 0;
        meta_data->generation_version = 0;

        bitmap = new ((void *)bitmap_addr) std::bitset<bitmap_size>;

        printf("[NVM MGR]\tinitialize nvm file's head\n");
    } else {
        meta_data->generation_version++;
        printf("nvm mgr restart, the free offset is %ld, generation version "
               "is %ld\n",
               meta_data->free_bit_offset, meta_data->generation_version);
    }
    }

}

NVMMgr::~NVMMgr() {
    // normally exit
    printf("[NVM MGR]\tnormally exits, NVM reset..\n");
    munmap((void *)start_addr, allocate_size);
    close(fd);
}

void *NVMMgr::alloc_thread_info() {
    // not thread safe
    size_t index = meta_data->threads++;
    return (void *)(thread_local_start + 2 * index * PGSIZE);
}

void *NVMMgr::alloc_block(int tid) {
    uint64_t id = __sync_fetch_and_add(&meta_data->free_bit_offset, 1);
    meta_data->bitmap[id] = tid;

    void *addr = (void *)(data_block_start + id * PGSIZE);

    return addr;
}

void NVMMgr::set_bitmap(void *addr) {
    long offset = (long)addr - data_block_start;
    bitmap->set(offset/512); // TODO 512
}

void NVMMgr::reset_bitmap(void *addr) {
    long offset = (long)addr - data_block_start;
    bitmap->reset(offset/512); // TODO 512
}

// TODO
// mutiple threads to recovery free list for
// threads using recovery_set
void NVMMgr::recovery_free_memory(int forward_thread) {
    int owner = 0;
    for (int i = 0; i < meta_data->free_bit_offset; i++) {
        meta_data->bitmap[i] = (owner++) % forward_thread;
    }
    std::cout << "finish set owner, all " << meta_data->free_bit_offset
              << " pages\n";

    const size_t power_two[10] = {8,   16,  32,   64,   128,
                                  256, 512, 1024, 2048, 4096};
    const int thread_num = 36;
    std::thread *tid[thread_num];
    int per_thread_block = meta_data->free_bit_offset / thread_num;
    if (meta_data->free_bit_offset % thread_num != 0)
        per_thread_block++;

    std::cout << "every thread needs to recover " << per_thread_block
              << " pages\n";

    for (int i = 0; i < thread_num; i++) {
        tid[i] = new std::thread(
            [&](int id) {
                // [start, end]
                uint64_t start = id * per_thread_block;
                uint64_t end = (id + 1) * per_thread_block;
                //                std::cout << "start " << start
                //                          << " end " << end<<"\n";
                uint64_t start_addr = data_block_start + start * PGSIZE;
                uint64_t end_addr = std::min(
                    data_block_start + end * PGSIZE,
                    data_block_start + meta_data->free_bit_offset * PGSIZE);
                //                std::set<std::pair<uint64_t, size_t>>
                //                    recovery_set; // used for memory recovery
                std::vector<std::pair<uint64_t, size_t>> recovery_set;

                // art->rebuild(recovery_set, start_addr, end_addr, id); // TODO
#ifdef RECLAIM_MEMORY

                std::sort(recovery_set.begin(), recovery_set.end());
                std::cout << "start to reclaim\n";
#endif
            },
            i);
    }
    for (int i = 0; i < thread_num; i++) {
        tid[i]->join();
    }
}

/*
 * interface to call methods of nvm_mgr
 */
NVMMgr *get_nvm_mgr(int workerid) {
    std::lock_guard<std::mutex> lock(_mtx);

    if (nvm_mgr == NULL) {
        printf("[NVM MGR]\tnvm manager is not initilized.\n");
        assert(0);
    }
    if ((workerid % 2) != 0){
    return nvm_mgr;
    }

    return nvm_mgr1;
}

bool init_nvm_mgr() {
    int tag = system((std::string("rm -rf ") + nvm_dir + "part.data").c_str());
    int tag1 = system((std::string("rm -rf ") + nvm_dir1 + "part.data").c_str());
    std::lock_guard<std::mutex> lock(_mtx);

    if (nvm_mgr) {
        printf("[NVM MGR]\tnvm manager has already been initilized.\n");
        return false;
    }
    nvm_mgr = new NVMMgr();
    nvm_mgr1 = new NVMMgr(true);
    return true;
}

void close_nvm_mgr() {
    std::lock_guard<std::mutex> lock(_mtx);
    std::cout << "[NVM MGR]\tclose nvm mgr\n";
    if (nvm_mgr != NULL) {
        delete nvm_mgr;
        nvm_mgr = NULL;
    }
}
} // namespace NVMMgr_ns
