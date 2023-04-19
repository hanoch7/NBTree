cmake_minimum_required(VERSION 2.4)
project(NBTree)

include(ExternalProject)

#set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS} -DPERF_LATENCY -DUSE_NVM_MALLOC -DNO_CONCURRENT -DNDEBUG -g -O2 -mrtm")
# set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}  -DUSE_NVM_MALLOC -DCLEAR_NVM_POOL -g -O2 -mrtm")
set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS} -DCLEAR_NVM_POOL -g -O2 -mrtm -march=native")

set(PMDK_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/pmdk")
ExternalProject_Add(PMDK
        # GIT_REPOSITORY https://github.com/HaoPatrick/pmdk.git
        GIT_REPOSITORY git@github.com:XiangpengHao/pmdk.git
        GIT_TAG addr-patch
       	BUILD_IN_SOURCE 1
        BUILD_COMMAND $(MAKE) EXTRA_CFLAGS=-Wno-error
        PREFIX ${PMDK_PREFIX}
        CONFIGURE_COMMAND ""
        INSTALL_COMMAND ""
        LOG_DOWNLOAD ON
        LOG_CONFIGURE ON
        LOG_BUILD ON
        )

include_directories(${PMDK_PREFIX}/src/PMDK/src/include)
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
  # link_directories(${PMDK_PREFIX}/src/PMDK/src/debug)
  link_directories(${PMDK_PREFIX}/src/PMDK/src/nondebug)
else ()
  link_directories(${PMDK_PREFIX}/src/PMDK/src/nondebug)
endif ()

link_directories("${PROJECT_SOURCE_DIR}/third-party-lib")
link_libraries("${PROJECT_SOURCE_DIR}/third-party-lib/libtbb.so.2")
link_directories(/usr/lib/x86_64-linux-gnu/)
link_libraries(boost_system boost_thread pthread pmemobj pmem)

include_directories("third-party-lib")
include_directories("include")
include_directories("nvm_mgr")
include_directories("pmdk")

add_executable(nbtree
	src/nbtree.cpp
	src/util.cpp
	nvm_mgr/Epoch.cpp
	nvm_mgr/nvm_mgr.cpp
	nvm_mgr/threadinfo.cpp
)

add_dependencies(nbtree PMDK)
# add_executable(data_generator
# 	src/data_generator.cpp
# 	src/util.cpp
# )



