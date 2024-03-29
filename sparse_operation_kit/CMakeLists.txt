#
# Copyright (c) 2021, NVIDIA CORPORATION.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.8)
project(SparseOperationKit LANGUAGES CXX CUDA)

message(STATUS "Building Sparse Operation Kit from source.")

set(TF_VERSION,"")
#check TF version
execute_process(COMMAND python -c "import tensorflow as tf;print(tf.__version__)" OUTPUT_VARIABLE TF_VERSION)
#message(STATUS "TF_VERSION = ${TF_VERSION}")
string(COMPARE EQUAL "${TF_VERSION}" "" TF_RESULT)
if (NOT TF_RESULT)
    string(REPLACE "." ";" TF_VERSION_LIST ${TF_VERSION})
    list(GET TF_VERSION_LIST 0 TF_VERSION_MAJOR)
    list(GET TF_VERSION_LIST 1 TF_VERSION_MINOR)
    list(GET TF_VERSION_LIST 2 TF_VERSION_PATCH)
    if(${TF_VERSION_MAJOR} GREATER 1 AND ${TF_VERSION_MINOR} GREATER 9)
        add_definitions(-DTF_GE_210)
        set(CMAKE_CXX_STANDARD 17)
    else()
        add_definitions(-DTF_LESS_210)
        set(CMAKE_CXX_STANDARD 14)
    endif()
else()
    message(FATAL_ERROR "Can not detect tensorflow in your environment,please install tensorflow(tf1 support version 1.15, for tf2 support version 2.60~latest) ")
endif()

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmakes)

find_package(CUDA REQUIRED)
find_package(NCCL REQUIRED)
find_package(TensorFlow REQUIRED)
find_package(MPI REQUIRED)
find_package(Threads)

set(CUDA_SEPARABLE_COMPILATION ON)

# whether use nvtx
option(USE_NVTX "Use nvtx for profiling" OFF)
if (USE_NVTX)
    message(STATUS "Add nvtx for profiling")
    add_definitions(-DUSE_NVTX)
    find_package(NVTX REQUIRED)
endif()

# whether make ops to be asynchronous
option(SOK_ASYNC "Make Ops to be asynchronous" ON)
if (SOK_ASYNC)
    message(STATUS "Make Ops to be asynchronous")
    add_definitions(-DSOK_ASYNC)
endif()

# whether build for DeepRec
option(ENABLE_DEEPREC "Enable op support for deeprec" OFF)
if (ENABLE_DEEPREC)
set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS}    -DGOOGLE_CUDA -DTENSORFLOW_USE_GPU_EV")
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS}  -DGOOGLE_CUDA -DTENSORFLOW_USE_GPU_EV")
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -DGOOGLE_CUDA -DTENSORFLOW_USE_GPU_EV")
endif()

# setting compiler flags
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
    message(STATUS "Setting default CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif()

foreach(arch_name ${SM})
    if (arch_name STREQUAL 80 OR
        arch_name STREQUAL 75 OR
        arch_name STREQUAL 70 OR
        arch_name STREQUAL 61 OR
        arch_name STREQUAL 60)
        list(APPEND cuda_arch_list ${arch_name})
        message(STATUS "Assign GPU architecture (sm=${arch_name})")
    else()
        message(FATAL_ERROR "Unknown or unsupported GPU architecture (set sm=70)")
    endif()
endforeach()

list(LENGTH cuda_arch_list cuda_arch_list_length)
if(${cuda_arch_list_length} EQUAL 0)
    list(APPEND cuda_arch_list "70")
    message(STATUS "Assign default GPU architecture sm=70")
endif()
list(REMOVE_DUPLICATES cuda_arch_list)

foreach(arch_name ${cuda_arch_list})
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -gencode arch=compute_${arch_name},code=sm_${arch_name}")
endforeach()

if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
    set(CMAKE_C_FLAGS    "${CMAKE_C_FLAGS} -Wall")
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -Wno-unknown-pragmas")
    if (${TF_VERSION_MAJOR} EQUAL 1 AND NOT ENABLE_DEEPREC)
        set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wno-error=class-memaccess")
    endif()
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Xcompiler -Wall,-Wno-error=cpp")
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Xcudafe --display_error_number")
    # set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Xcudafe --diag_suppress=esa_on_defaulted_function_ignored")
else()
    # -------- set flags for DEBUG mode --- #
    set(CMAKE_CXX_FLAGS_DEBUG  "${CMAKE_CXX_FLAGS_DEBUG} -O0 -Wno-unknown-pragmas")
    if (${TF_VERSION_MAJOR} EQUAL 1)
        set(CMAKE_CXX_FLAGS_DEBUG  "${CMAKE_CXX_FLAGS_DEBUG} -O0 -Wno-error=class-memaccess")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -Wno-error=class-memaccess")
    endif()
    add_definitions(-DNDEBUG)
    set(CMAKE_CUDA_FLAGS_DEBUG "${CMAKE_CUDA_FLAGS_DEBUG} -Xcompiler -Wall,-Wno-error=cpp")
    set(CMAKE_CUDA_FLAGS_DEBUG "${CMAKE_CUDA_FLAGS_DEBUG} -Xcudafe --display_error_number")
    # set(CMAKE_CUDA_FLAGS_DEBUG "${CMAKE_CUDA_FLAGS_DEBUG} -Xcudafe --diag_suppress=esa_on_defaulted_function_ignored")
    # ------------------------------------- #
endif()

set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --expt-extended-lambda --expt-relaxed-constexpr")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TF_COMPILE_FLAGS}")
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -DGOOGLE_CUDA=1 ${TF_COMPILE_FLAGS}")

# setting output folder
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# setting install folder
if(SKBUILD)
    # if we're building with skbuild, then we shouldn't be installing to /usr/local/lib
    # install the libraries alongside the python source in sparse_operation_kit/lib instead
    install(DIRECTORY ${CMAKE_BINARY_DIR}/lib DESTINATION sparse_operation_kit)
else()
    install(DIRECTORY ${CMAKE_BINARY_DIR}/lib DESTINATION /usr/local)
endif()

# headers
if(SKBUILD)
# We haven't found a proper way to maintain the directory structure of
# the parent folder(i.e. HugeCTR) when using skbuild to make pip package,
# so we use a workaround here: copy the content of parent folder into
# sparse_operation_kit/ before making pip package.
include_directories(
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/include/
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/third_party/
    ${PROJECT_SOURCE_DIR}/kit_cc/framework/kernels/
    ${PROJECT_SOURCE_DIR}/kit_cc_impl/embedding/
    ${PROJECT_SOURCE_DIR}/
    ${PROJECT_SOURCE_DIR}/HugeCTR/
    ${PROJECT_SOURCE_DIR}/HugeCTR/include
    ${PROJECT_SOURCE_DIR}/HugeCTR/core
    ${PROJECT_SOURCE_DIR}/HugeCTR/embedding
    ${PROJECT_SOURCE_DIR}/third_party/json/include
    ${CUDA_INCLUDE_DIRS}
    ${NCCL_INC_PATHS}
    ${MPI_INCLUDE_PATH}
)
else()
include_directories(
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/include/
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/third_party/
    ${PROJECT_SOURCE_DIR}/kit_cc/framework/kernels/
    ${PROJECT_SOURCE_DIR}/kit_cc_impl/embedding/
    ${PROJECT_SOURCE_DIR}/../
    ${PROJECT_SOURCE_DIR}/../HugeCTR/
    ${PROJECT_SOURCE_DIR}/../HugeCTR/include
    ${PROJECT_SOURCE_DIR}/../HugeCTR/core
    ${PROJECT_SOURCE_DIR}/../HugeCTR/embedding
    ${PROJECT_SOURCE_DIR}/../third_party/json/include
    ${CUDA_INCLUDE_DIRS}
    ${NCCL_INC_PATHS}
    ${MPI_INCLUDE_PATH}
)
endif()


# libs
link_directories(
    ${NCCL_LIB_PATHS}
    ${TF_LINK_DIR}/
    /usr/local/cuda/lib64/
)

# code sources related to framework
file(GLOB framework
    ${PROJECT_SOURCE_DIR}/kit_cc/framework/kernels/*.cpp
    ${PROJECT_SOURCE_DIR}/kit_cc/framework/kernels/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc/framework/kernels/*.cu
    ${PROJECT_SOURCE_DIR}/kit_cc/framework/ops/*.cpp
    ${PROJECT_SOURCE_DIR}/kit_cc/framework/ops/*.cc
)

# code sources related to the kit infrastructure
file(GLOB kit_infra
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/*.cpp
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/*.cu
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/resources/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/parameters/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/parameters/*.cu
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/hashtable/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/hashtable/*.cu
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/embeddings/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/embeddings/*.cu
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/tensor_buffer/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/dispatcher/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/dispatcher/*.cu
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/initializer/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/initializer/*.cu
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/operation/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/operation/*.cu
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/optimizer/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc/kit_cc_infra/src/optimizer/*.cu
)

# code sources for the concrete algorithm implementations of the kit
file(GLOB kit_impl
    ${PROJECT_SOURCE_DIR}/kit_cc_impl/embedding/common/src/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc_impl/embedding/common/src/*.cu
    ${PROJECT_SOURCE_DIR}/kit_cc_impl/embedding/dispatcher/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc_impl/embedding/dispatcher/*.cu
    ${PROJECT_SOURCE_DIR}/kit_cc_impl/embedding/lookuper/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc_impl/embedding/lookuper/*.cu
    ${PROJECT_SOURCE_DIR}/kit_cc_impl/embedding/operation/*.cc
    ${PROJECT_SOURCE_DIR}/kit_cc_impl/embedding/operation/*.cu
)

file(GLOB test_op
    ${PROJECT_SOURCE_DIR}/Plugin/framework/kernels/test_op.cc
    ${PROJECT_SOURCE_DIR}/Plugin/framework/ops/test_op.cc
)


# build dynamic lib
add_library(sparse_operation_kit SHARED ${framework} ${kit_infra} ${kit_impl})
target_link_libraries(sparse_operation_kit PUBLIC ${TF_LINK_FLAGS} nccl cusparse ${MPI_CXX_LIBRARIES})

if(SKBUILD)
# We haven't found a proper way to maintain the directory structure of
# the parent folder(i.e. HugeCTR) when using skbuild to make pip package,
# so we use a workaround here: copy the content of parent folder into
# sparse_operation_kit/ before making pip package.
configure_file(${PROJECT_SOURCE_DIR}/HugeCTR/include/config.hpp.in ${PROJECT_SOURCE_DIR}/HugeCTR/include/config.hpp)
add_subdirectory(HugeCTR/core core.out)
add_subdirectory(HugeCTR/embedding embedding.out)
else()
configure_file(${PROJECT_SOURCE_DIR}/../HugeCTR/include/config.hpp.in ${PROJECT_SOURCE_DIR}/../HugeCTR/include/config.hpp)
add_subdirectory(../HugeCTR/core core.out)
add_subdirectory(../HugeCTR/embedding embedding.out)
endif()
# file(GLOB core_impl
#     ${PROJECT_SOURCE_DIR}/experiment/lookup/impl/core_impl/*.cpp
# )
# add_library(tf_impl_ops_test SHARED ${core_impl})
# target_link_libraries(tf_impl_ops_test PUBLIC ${TF_LINK_FLAGS} nccl cusparse ${MPI_CXX_LIBRARIES} core embedding)

if (USE_NVTX)
    target_link_libraries(sparse_operation_kit PUBLIC ${NVTX_LIB})
endif()

set_target_properties(sparse_operation_kit PROPERTIES CUDA_RESOLVE_DEVICE_SYMBOLS ON)
set_target_properties(sparse_operation_kit PROPERTIES CUDA_ARCHITECTURES OFF)

# compile compat ops
add_subdirectory(kit_cc_impl/operations)

# compile ops for unit test
option(SOK_UNIT_TEST "Compile SOK unit test" ON)
if (SOK_UNIT_TEST)
    message(STATUS "Compile SOK unit test")
    add_subdirectory(unit_test)
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/experiment)
