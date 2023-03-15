#include "nvm_mgr.h"
// #include "Tree.h"
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

NVMMgr::NVMMgr() {
    // access 的返回结果， 0: 存在， 1: 不存在
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

    // mmap
    void *addr = mmap((void *)start_addr, allocate_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);

    if (addr != (void *)start_addr) {
        printf("[NVM MGR]\tmmap failed %p \n", addr);
        exit(0);
    }
    printf("[NVM MGR]\tmmap successfully\n");

    // initialize meta data
    meta_data = static_cast<Head *>(addr);
    if (initial) {
        // set status of head and set zero for bitmap
        // persist it
        std::cout<< "addr of meta_data: " << meta_data << "\n";
        memset((void *)meta_data, 0, PGSIZE);

        meta_data->status = magic_number;
        meta_data->threads = 0;
        meta_data->free_bit_offset = 0;
        meta_data->generation_version = 0;

        // flush_data((void *)meta_data, PGSIZE);
        printf("[NVM MGR]\tinitialize nvm file's head\n");
    } else {
        meta_data->generation_version++;
        // flush_data((void *)&meta_data->generation_version, sizeof(uint64_t));
        printf("nvm mgr restart, the free offset is %ld, generation version "
               "is %ld\n",
               meta_data->free_bit_offset, meta_data->generation_version);
    }

    // initialize bitmap
    // bitmap = static_cast<std::bitset<bitmap_size> *>(addr+PGSIZE);
    // std::bitset<bitmap_size> *tmp_bitmap;
    if (initial) {
        bitmap = new ((void *)bitmap_addr) std::bitset<bitmap_size>;
        std::cout<< "addr of bitmap: " << bitmap << "\n";
        // memcpy(bitmap,tmp_bitmap,sizeof(std::bitset<bitmap_size>));
        // flush_data((void *)bitmap, sizeof(std::bitset<bitmap_size>));
        std::cout << sizeof(std::bitset<bitmap_size>) << "\n";
        std::cout << bitmap_size << "\n";
        std::cout << bitmap_size / sizeof(std::bitset<bitmap_size>) << "\n";
        // std::cout << bitmap->test(0) << "\n";
        // bitmap->set(0);
        // std::cout << bitmap->test(0) << "\n";
    } else {
        // TODO
    }
    // delete tmp_bitmap;

}

NVMMgr::~NVMMgr() {
    // normally exit
    printf("[NVM MGR]\tnormally exits, NVM reset..\n");
    //        Head *head = (Head *) start_addr;
    //        flush_data((void *) head, sizeof(Head));
    munmap((void *)start_addr, allocate_size);
    close(fd);
}

void *NVMMgr::alloc_thread_info() {
    // not thread safe
    size_t index = meta_data->threads++;
    // flush_data((void *)&(meta_data->threads), sizeof(int));
    return (void *)(thread_local_start + 2 * index * PGSIZE);
}

void *NVMMgr::get_thread_info(int tid) {
    return (void *)(thread_local_start + 2 * tid * PGSIZE);
}

void *NVMMgr::alloc_block(int tid) {
    uint64_t id = __sync_fetch_and_add(&meta_data->free_bit_offset, 1);
    // // std::cout << "id: " << id << "\n";

    // std::lock_guard<std::mutex> lock(_mtx);

    // uint64_t id = meta_data->free_bit_offset;
    // meta_data->free_bit_offset++;
    meta_data->bitmap[id] = tid;
    // flush_data((void *)&(meta_data->bitmap[id]), sizeof(uint8_t));
    // flush_data((void *)&(meta_data->free_bit_offset), sizeof(uint64_t));


    void *addr = (void *)(data_block_start + id * PGSIZE);

    // printf("[NVM MGR]\talloc a new block %d, type is %d\n", id, type);
    // std::cout<<"alloc a new block "<< meta_data->free_bit_offset<<"\n";
    // std::cout<<"meta data addr "<< meta_data<<"\n";
    // std::cout<<"mgr addr" <<this<<"\n";

    return addr;
}

void NVMMgr::set_bitmap(void *addr) {
    long offset = (long)addr - data_block_start;
    bitmap->set(offset/512); // TODO 512
    // std::cout << "set" << offset/512 << "\n";
    // std::chrono::milliseconds duration(100);
    // std::this_thread::sleep_for(duration);
}

void NVMMgr::reset_bitmap(void *addr) {
    long offset = (long)addr - data_block_start;
    bitmap->reset(offset/512); // TODO 512
    // std::cout << "reset" << offset/512 << "\n";
    // std::chrono::milliseconds duration(100);
    // std::this_thread::sleep_for(duration);
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

//                for (int i = 0; i < recovery_set.size(); i++) {
//                    uint64_t this_addr = recovery_set[i].first;
//                    uint64_t this_size = recovery_set[i].second;
//                    for (int id = 0; id < 10; id++) {
//                        if (this_size <= power_two[id]) {
//                            this_size = power_two[id];
//                            break;
//                        }
//                    }
//
//                    int j = start_addr / PGSIZE;
//                    int tid =
//                        meta_data
//                            ->bitmap[j]; // this block belong to which thread
//                    thread_info *the_ti = (thread_info *)get_thread_info(tid);
//
//                    the_ti->free_list->insert_into_freelist(
//                        start_addr, this_addr - start_addr);
//                    start_addr = this_addr + this_size;
//                }
//
//                int j = start_addr / PGSIZE;
//                int tid =
//                    meta_data->bitmap[j]; // this block belong to which thread
//                thread_info *the_ti = (thread_info *)get_thread_info(tid);
//
//                if (end_addr >= start_addr) {
//                    the_ti->free_list->insert_into_freelist(
//                        start_addr, end_addr - start_addr);
//                }
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
NVMMgr *get_nvm_mgr() {
    std::lock_guard<std::mutex> lock(_mtx);

    if (nvm_mgr == NULL) {
        printf("[NVM MGR]\tnvm manager is not initilized.\n");
        assert(0);
    }
    return nvm_mgr;
}

bool init_nvm_mgr() {
    int tag = system((std::string("rm -rf ") + nvm_dir + "part.data").c_str());
    std::lock_guard<std::mutex> lock(_mtx);

    if (nvm_mgr) {
        printf("[NVM MGR]\tnvm manager has already been initilized.\n");
        return false;
    }
    nvm_mgr = new NVMMgr();
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
