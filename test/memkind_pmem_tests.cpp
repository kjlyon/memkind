/*
 * Copyright (C) 2015 - 2018 Intel Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice(s),
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice(s),
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <memkind/internal/memkind_pmem.h>
#include <memkind/internal/memkind_private.h>
#include "allocator_perf_tool/TimerSysTime.hpp"

#include <sys/param.h>
#include <sys/mman.h>
#include <stdio.h>
#include <pthread.h>
#include "common.h"

static const size_t PMEM_PART_SIZE = MEMKIND_PMEM_MIN_SIZE + 4096;
static const size_t PMEM_NO_LIMIT = 0;
extern const char*  PMEM_DIR;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

class MemkindPmemTests: public :: testing::Test
{

protected:
    memkind_t pmem_kind;
    void SetUp()
    {
        // create PMEM partition
        int err = memkind_create_pmem(PMEM_DIR, PMEM_PART_SIZE, &pmem_kind);
        ASSERT_EQ(0, err);
        ASSERT_TRUE(nullptr != pmem_kind);
    }

    void TearDown()
    {
        int err = memkind_destroy_kind(pmem_kind);
        ASSERT_EQ(0, err);
    }
};

class MemkindPmemTestsCalloc : public MemkindPmemTests,
    public ::testing::WithParamInterface<std::tuple<int, int>>
{
};

class MemkindPmemTestsMalloc : public MemkindPmemTests,
    public ::testing::WithParamInterface<size_t>
{
};

static void pmem_get_size(struct memkind *kind, size_t& total, size_t& free)
{
    struct memkind_pmem *priv = reinterpret_cast<struct memkind_pmem *>(kind->priv);

    total = priv->max_size;
    free = priv->max_size - priv->offset; /* rough estimation */
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemPriv)
{
    size_t total_mem = 0;
    size_t free_mem = 0;

    pmem_get_size(pmem_kind, total_mem, free_mem);

    ASSERT_TRUE(total_mem != 0);
    ASSERT_TRUE(free_mem != 0);

    EXPECT_EQ(total_mem, roundup(PMEM_PART_SIZE, MEMKIND_PMEM_CHUNK_SIZE));

    size_t offset = total_mem - free_mem;
    EXPECT_LT(offset, MEMKIND_PMEM_CHUNK_SIZE);
    EXPECT_LT(offset, total_mem);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemMalloc)
{
    const size_t size = 1024;
    char *default_str = nullptr;

    default_str = (char *)memkind_malloc(pmem_kind, size);
    EXPECT_TRUE(nullptr != default_str);

    sprintf(default_str, "memkind_malloc MEMKIND_PMEM\n");
    printf("%s", default_str);

    memkind_free(pmem_kind, default_str);

    // Out of memory
    default_str = (char *)memkind_malloc(pmem_kind, 2 * PMEM_PART_SIZE);
    EXPECT_EQ(nullptr, default_str);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemMallocZero)
{
    void *test1 = nullptr;

    test1 = memkind_malloc(pmem_kind, 0);
    ASSERT_TRUE(test1 == nullptr);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemMallocSizeMax)
{
    void *test1 = nullptr;

    errno = 0;
    test1 = memkind_malloc(pmem_kind, SIZE_MAX);
    ASSERT_TRUE(test1 == nullptr);
    ASSERT_TRUE(errno == ENOMEM);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemCalloc)
{
    const size_t size = 1024;
    const size_t num = 1;
    char *default_str = nullptr;

    default_str = (char *)memkind_calloc(pmem_kind, num, size);
    EXPECT_TRUE(nullptr != default_str);
    EXPECT_EQ(*default_str, 0);

    sprintf(default_str, "memkind_calloc MEMKIND_PMEM\n");
    printf("%s", default_str);

    memkind_free(pmem_kind, default_str);

    // allocate the buffer of the same size (likely at the same address)
    default_str = (char *)memkind_calloc(pmem_kind, num, size);
    EXPECT_TRUE(nullptr != default_str);
    EXPECT_EQ(*default_str, 0);

    sprintf(default_str, "memkind_calloc MEMKIND_PMEM\n");
    printf("%s", default_str);

    memkind_free(pmem_kind, default_str);
}

/*
 * Test will check if it is not possible to allocate memory
 * with calloc arguments size or num equal to zero
 */
TEST_P(MemkindPmemTestsCalloc, test_TC_MEMKIND_PmemCallocZero)
{
    void *test = nullptr;
    size_t size = std::get<0>(GetParam());
    size_t num = std::get<1>(GetParam());

    test = memkind_calloc(pmem_kind, size, num);
    ASSERT_TRUE(test == nullptr);
}

INSTANTIATE_TEST_CASE_P(
    CallocParam, MemkindPmemTestsCalloc,
    ::testing::Values(std::make_tuple(10, 0),
                      std::make_tuple(0, 0),
                      std::make_tuple(0, 10)));

TEST_P(MemkindPmemTestsCalloc, test_TC_MEMKIND_PmemCallocSizeMax)
{
    void *test = nullptr;
    size_t size = SIZE_MAX;
    size_t num = 1;
    errno = 0;

    test = memkind_calloc(pmem_kind, size, num);
    ASSERT_TRUE(test == nullptr);
    ASSERT_TRUE(errno == ENOMEM);
}

TEST_P(MemkindPmemTestsCalloc, test_TC_MEMKIND_PmemCallocNumMax)
{
    void *test = nullptr;
    size_t size = 10;
    size_t num = SIZE_MAX;
    errno = 0;

    test = memkind_calloc(pmem_kind, size, num);
    ASSERT_TRUE(test == nullptr);
    ASSERT_TRUE(errno == ENOMEM);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemCallocHuge)
{
    const size_t size = MEMKIND_PMEM_CHUNK_SIZE;
    const size_t num = 1;
    char *default_str = nullptr;

    default_str = (char *)memkind_calloc(pmem_kind, num, size);
    EXPECT_TRUE(nullptr != default_str);
    EXPECT_EQ(*default_str, 0);

    sprintf(default_str, "memkind_calloc MEMKIND_PMEM\n");
    printf("%s", default_str);

    memkind_free(pmem_kind, default_str);

    // allocate the buffer of the same size (likely at the same address)
    default_str = (char *)memkind_calloc(pmem_kind, num, size);
    EXPECT_TRUE(nullptr != default_str);
    EXPECT_EQ(*default_str, 0);

    sprintf(default_str, "memkind_calloc MEMKIND_PMEM\n");
    printf("%s", default_str);

    memkind_free(pmem_kind, default_str);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemRealloc)
{
    const size_t size1 = 512;
    const size_t size2 = 1024;
    char *default_str = nullptr;

    default_str = (char *)memkind_realloc(pmem_kind, default_str, size1);
    EXPECT_TRUE(nullptr != default_str);

    sprintf(default_str, "memkind_realloc MEMKIND_PMEM with size %zu\n", size1);
    printf("%s", default_str);

    default_str = (char *)memkind_realloc(pmem_kind, default_str, size2);
    EXPECT_TRUE(nullptr != default_str);

    sprintf(default_str, "memkind_realloc MEMKIND_PMEM with size %zu\n", size2);
    printf("%s", default_str);

    memkind_free(pmem_kind, default_str);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemMallocUsableSize)
{
    const struct {
        size_t size;
        size_t spacing;
    } check_sizes[] = {
        {.size = 10, .spacing = 8},
        {.size = 100, .spacing = 16},
        {.size = 200, .spacing = 32},
        {.size = 500, .spacing = 64},
        {.size = 1000, .spacing = 128},
        {.size = 2000, .spacing = 256},
        {.size = 3000, .spacing = 512},
        {.size = 1 * 1024 * 1024, .spacing = 4 * 1024 * 1024},
        {.size = 2 * 1024 * 1024, .spacing = 4 * 1024 * 1024},
        {.size = 3 * 1024 * 1024, .spacing = 4 * 1024 * 1024},
        {.size = 4 * 1024 * 1024, .spacing = 4 * 1024 * 1024}
    };
    struct memkind *pmem_temp = nullptr;

    int err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE, &pmem_temp);
    EXPECT_EQ(0, err);
    EXPECT_TRUE(nullptr != pmem_temp);
    size_t usable_size = memkind_malloc_usable_size(pmem_temp, nullptr);
    EXPECT_EQ(0u, usable_size);
    for (unsigned int i = 0; i < (sizeof(check_sizes) / sizeof(check_sizes[0]));
         ++i) {
        size_t size = check_sizes[i].size;
        void *alloc = memkind_malloc(pmem_temp, size);
        EXPECT_TRUE(nullptr != alloc);
        usable_size = memkind_malloc_usable_size(pmem_temp, alloc);
        size_t diff = usable_size - size;
        EXPECT_GE(usable_size, size);
        EXPECT_LE(diff, check_sizes[i].spacing);
        memkind_free(pmem_temp, alloc);
    }
    err = memkind_destroy_kind(pmem_temp);
    EXPECT_EQ(0, err);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemResize)
{
    const size_t size = MEMKIND_PMEM_CHUNK_SIZE;
    char *pmem_str10 = nullptr;
    char *pmem_strX = nullptr;
    memkind_t pmem_kind_no_limit = nullptr;
    memkind_t pmem_kind_not_possible = nullptr;
    int err = 0;

    pmem_str10 = (char *)memkind_malloc(pmem_kind, MEMKIND_PMEM_MIN_SIZE);
    EXPECT_TRUE(nullptr != pmem_str10);

    // Out of memory
    pmem_strX = (char *)memkind_malloc(pmem_kind, size);
    EXPECT_TRUE(nullptr == pmem_strX);

    memkind_free(pmem_kind, pmem_str10);
    memkind_free(pmem_kind, pmem_strX);

    err = memkind_create_pmem(PMEM_DIR, PMEM_NO_LIMIT, &pmem_kind_no_limit);
    EXPECT_EQ(0, err);
    EXPECT_TRUE(nullptr != pmem_kind_no_limit);

    pmem_str10 = (char *)memkind_malloc(pmem_kind_no_limit, MEMKIND_PMEM_MIN_SIZE);
    EXPECT_TRUE(nullptr != pmem_str10);

    pmem_strX = (char *)memkind_malloc(pmem_kind_no_limit, size);
    EXPECT_TRUE(nullptr != pmem_strX);

    memkind_free(pmem_kind_no_limit, pmem_str10);
    memkind_free(pmem_kind_no_limit, pmem_strX);

    err = memkind_destroy_kind(pmem_kind_no_limit);
    EXPECT_EQ(0, err);

    err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE-1,
                              &pmem_kind_not_possible);
    EXPECT_EQ(MEMKIND_ERROR_INVALID, err);
    EXPECT_TRUE(nullptr == pmem_kind_not_possible);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemReallocZero)
{
    size_t size = 1024;
    void *test = nullptr;
    void *new_test = nullptr;

    test = memkind_malloc(pmem_kind, size);
    ASSERT_TRUE(test != nullptr);

    new_test = memkind_realloc(pmem_kind, test, 0);
    ASSERT_TRUE(new_test == nullptr);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemReallocSizeMax)
{
    size_t size = 1024;
    void *test = nullptr;
    void *new_test = nullptr;

    test = memkind_malloc(pmem_kind, size);
    ASSERT_TRUE(test != nullptr);
    errno = 0;
    new_test = memkind_realloc(pmem_kind, test, SIZE_MAX);
    ASSERT_TRUE(new_test == nullptr);
    ASSERT_TRUE(errno == ENOMEM);

    memkind_free(pmem_kind, test);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemReallocNullptr)
{
    size_t size = 1024;
    void *test = nullptr;

    test = memkind_realloc(pmem_kind, test, size);
    ASSERT_TRUE(test != nullptr);

    memkind_free(pmem_kind, test);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemReallocNullptrZero)
{
    void *test = nullptr;

    test = memkind_realloc(pmem_kind, test, 0);
    ASSERT_TRUE(test == nullptr);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemReallocIncreaseSize)
{
    size_t size = 1024;
    char *test1 = nullptr;
    char *test2 = nullptr;
    const char val[] = "test_TC_MEMKIND_PmemReallocIncreaseSize";
    int status;

    test1 = (char*)memkind_malloc(pmem_kind, size);
    ASSERT_TRUE(test1 != nullptr);

    sprintf(test1, "%s", val);

    size *= 2;
    test2 = (char*)memkind_realloc(pmem_kind, test1, size);
    ASSERT_TRUE(test2 != nullptr);
    status = memcmp(val, test2, sizeof(val));
    ASSERT_TRUE(status == 0);

    memkind_free(pmem_kind, test2);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemReallocDecreaseSize)
{
    size_t size = 1024;
    char *test1 = nullptr;
    char *test2 = nullptr;
    const char val[] = "test_TC_MEMKIND_PmemReallocDecreaseSize";
    int status;

    test1 = (char*)memkind_malloc(pmem_kind, size);
    ASSERT_TRUE(test1 != nullptr);

    sprintf(test1, "%s", val);

    size = 4;
    test2 = (char*)memkind_realloc(pmem_kind, test1, size);
    ASSERT_TRUE(test2 != nullptr);
    status = memcmp(val, test2, size);
    ASSERT_TRUE(status == 0);

    memkind_free(pmem_kind, test2);
}

/*
 * This test shows realloc "in-place" mechanism.
 * In some cases like allocation shrinking within the same size class
 * realloc will shrink allocation "in-place",
 * but in others not (like changing size class).
 */
TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemReallocInPlace)
{
    void *test1 = memkind_malloc(pmem_kind, 10 * 1024 * 1024);
    ASSERT_TRUE(test1 != nullptr);

    /* Several reallocations within the same jemalloc size class*/
    void *test1r = memkind_realloc(pmem_kind, test1, 6 * 1024 * 1024);
    ASSERT_EQ(test1r, test1);

    test1r = memkind_realloc(pmem_kind, test1, 10 * 1024 * 1024);
    ASSERT_EQ(test1r, test1);

    test1r = memkind_realloc(pmem_kind, test1, 8 * 1024 * 1024);
    ASSERT_EQ(test1r, test1);

    void *test2 = memkind_malloc(pmem_kind, 4 * 1024 * 1024);
    ASSERT_TRUE(test2 != nullptr);

    /* 4MB => 16B (changing size class) */
    void *test2r = memkind_realloc(pmem_kind, test2, 16);
    ASSERT_TRUE(test2r != nullptr);

    /* 8MB => 16B */
    test1r = memkind_realloc(pmem_kind, test1, 16);

    /*
     * If the old size of the allocation is larger than
     * the chunk size (4MB), we can reallocate it to 4MB first (in place),
     * releasing some space, which makes it possible to do the actual
     * shrinking...
     */
    ASSERT_TRUE(test1r != nullptr);
    ASSERT_NE(test1r, test1);

    /* ... and leaves some memory for new allocations. */
    void *test3 = memkind_malloc(pmem_kind, 5 * 1024 * 1024);
    ASSERT_TRUE(test3 != nullptr);

    memkind_free(pmem_kind, test1r);
    memkind_free(pmem_kind, test2r);
    memkind_free(pmem_kind, test3);
}

/*
 * This test shows that we can make a single highest possible allocation
 * and there still will be a place for another allocations.
 */
TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemMaxFill)
{
    const int possible_alloc_max = 4;
    void *test[possible_alloc_max+1] = {nullptr};
    size_t total_mem = 0;
    size_t free_mem = 0;
    int i, j;

    pmem_get_size(pmem_kind, total_mem, free_mem);

    for (i = 0; i < possible_alloc_max; i++) {
        for (j = total_mem; j > 0; --j) {
            test[i] = memkind_malloc(pmem_kind, j);
            if(test[i] != nullptr)
                break;
        }
        ASSERT_NE(j, 0);
    }

    for (j = total_mem; j > 0; --j) {
        test[possible_alloc_max] = memkind_malloc(pmem_kind, j);
        if(test[possible_alloc_max] != nullptr)
            break;
    }
    //Ensure there is no more space available on kind
    ASSERT_EQ(j, 0);

    for (i = 0; i < possible_alloc_max; i++) {
        memkind_free(pmem_kind, test[i]);
    }
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemFreeNullptr)
{
    const double test_time = 5;

    TimerSysTime timer;
    timer.start();
    do {
        memkind_free(pmem_kind, nullptr);
    } while(timer.getElapsedTime() < test_time);
}

/*
 * This test will stress pmem kind with malloc-free loop
 * with various sizes for malloc
 */
TEST_P(MemkindPmemTestsMalloc, test_TC_MEMKIND_PmemMallocSize)
{
    const int malloc_limit = 1000000;
    const int loop_limit = 10;
    int first_limit_of_allocations = 0;
    int temp_limit_of_allocations = 0;
    void *test[malloc_limit] = {nullptr};
    int i = 0, j = 0;

    //check maximum number of allocations right after create the kind
    for (i = 0; i < malloc_limit; i++) {
        test[i] = memkind_malloc(pmem_kind, GetParam());
        if(test[i] == nullptr)
            break;
    }

    ASSERT_TRUE(malloc_limit != i);
    first_limit_of_allocations = i;

    for (i = 0; i < first_limit_of_allocations; i++) {
        memkind_free(pmem_kind, test[i]);
        test[i] = nullptr;
    }

    //check number of allocations in consecutive iterations of malloc-free loop
    for (i = 0; i < loop_limit; i++) {
        for (j = 0; j < malloc_limit; j++) {
            test[j] = memkind_malloc(pmem_kind, GetParam());
            if(test[j] == nullptr)
                break;
        }

        ASSERT_TRUE(malloc_limit != j);

        temp_limit_of_allocations = j;

        for (j = 0; j < temp_limit_of_allocations; j++) {
            memkind_free(pmem_kind, test[j]);
            test[j] = nullptr;
        }

        ASSERT_TRUE(temp_limit_of_allocations > 0.98 * first_limit_of_allocations);
    }
}

INSTANTIATE_TEST_CASE_P(
    MallocParam, MemkindPmemTestsMalloc,
    ::testing::Values(32, 60, 80, 100, 128, 150, 160, 250, 256, 300, 320,
                      500, 512, 800, 896, 3000, 4096, 6000, 10000, 60000,
                      98304, 114688, 131072, 163840, 196608, 500000,
                      2*1024*1024, 5*1024*1024));

TEST_F(MemkindPmemTests,
       test_TC_MEMKIND_PmemPosixMemalignWrongAlignmentLessThanVoidAndNotPowerOfTwo)
{
    void *test = nullptr;
    size_t size = 32;
    size_t wrong_alignment = 3;
    int ret;

    ret = memkind_posix_memalign(pmem_kind, &test, wrong_alignment, size);
    ASSERT_TRUE(ret == EINVAL);
    ASSERT_TRUE(test == nullptr);
}

TEST_F(MemkindPmemTests,
       test_TC_MEMKIND_PmemPosixMemalignWrongAlignmentLessThanVoidAndPowerOfTwo)
{
    void *test = nullptr;
    size_t size = 32;
    size_t wrong_alignment = sizeof(void*)/2;
    int ret;

    ret = memkind_posix_memalign(pmem_kind, &test, wrong_alignment, size);
    ASSERT_TRUE(ret == EINVAL);
    ASSERT_TRUE(test == nullptr);
}

TEST_F(MemkindPmemTests,
       test_TC_MEMKIND_PmemPosixMemalignWrongAlignmentNotPowerOfTwo)
{
    void *test = nullptr;
    size_t size = 32;
    size_t wrong_alignment = sizeof(void*)+1;
    int ret;

    ret = memkind_posix_memalign(pmem_kind, &test, wrong_alignment, size);
    ASSERT_TRUE(ret == EINVAL);
    ASSERT_TRUE(test == nullptr);
}

TEST_F(MemkindPmemTests,
       test_TC_MEMKIND_PmemPosixMemalignLowestCorrectAlignment)
{
    void *test = nullptr;
    size_t size = 32;
    size_t alignment = sizeof(void*);
    int ret;

    ret = memkind_posix_memalign(pmem_kind, &test, alignment, size);
    ASSERT_TRUE(ret == 0);
    ASSERT_TRUE(test != nullptr);

    memkind_free(pmem_kind, test);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemPosixMemalignSizeZero)
{
    void *test = nullptr;
    size_t alignment = sizeof(void*);
    int ret;

    ret = memkind_posix_memalign(pmem_kind, &test, alignment, 0);
    ASSERT_TRUE(ret != 0);
    ASSERT_TRUE(test == nullptr);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemPosixMemalignSizeMax)
{
    void *test = nullptr;
    size_t alignment = 64;
    int ret;

    ret = memkind_posix_memalign(pmem_kind, &test, alignment, SIZE_MAX);
    ASSERT_TRUE(ret == ENOMEM);
    ASSERT_TRUE(test == nullptr);
}

/*
 * This is a basic alignment test which will make alignment allocations,
 * check pointers, write and read values from allocated memory
 */
TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemPosixMemalign)
{
    const int max_allocs = 1000000;
    const int test_value = 123456;
    const int test_loop = 10;
    uintptr_t alignment;
    unsigned malloc_counter = 0;
    unsigned i = 0, j = 0;
    int *ptrs[max_allocs] = {nullptr};
    void *test = nullptr;
    int ret;

    for(alignment = 1024; alignment <= 131072; alignment *= 2) {
        for (i = 0; i < test_loop; i++) {
            for (j = 0; j < max_allocs; ++j) {
                errno = 0;
                ret = memkind_posix_memalign(pmem_kind, &test, alignment, sizeof(int *));
                if(ret != 0) {
                    //at least one allocation must succeed
                    //ASSERT_TRUE(j > 0); TODO: this is issue with posix_mem_align and test should be updated after resolving this, check PR#86
                    malloc_counter = j;
                    break;
                }

                EXPECT_EQ(ret, 0);
                EXPECT_EQ(errno, 0);

                ptrs[j] = (int *)test;

                //test pointer should be usable
                *(int*)test = test_value;
                ASSERT_EQ(*(int*)test, test_value);

                //check for correct address alignment
                ASSERT_EQ((uintptr_t)(test) & (alignment - 1), (uintptr_t)0);
            }

            for (j = 0; j < malloc_counter; ++j) {
                memkind_free(pmem_kind, ptrs[j]);
                ptrs[j] = nullptr;
            }
        }
    }
}

static memkind_t *pools;
static int npools = 3;
static void* thread_func(void* arg)
{
    int start_idx = *(int *)arg;
    int err = 0;
    for (int idx = 0; idx < npools; ++idx) {
        int pool_id = start_idx + idx;

        if (pools[pool_id] == nullptr) {
            err = memkind_create_pmem(PMEM_DIR, PMEM_PART_SIZE, &pools[pool_id]);
            EXPECT_EQ(0, err);
        }

        if (err == 0) {
            void *test = memkind_malloc(pools[pool_id], sizeof(void *));
            EXPECT_TRUE(test != nullptr);
            memkind_free(pools[pool_id], test);
            memkind_destroy_kind(pools[pool_id]);
        }
    }

    return nullptr;
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemMultithreads)
{
    int nthreads = 10, status = 0;
    pthread_t *threads = (pthread_t*)calloc(nthreads, sizeof(pthread_t));
    ASSERT_TRUE(threads != nullptr);
    int *pool_idx = (int*)calloc(nthreads, sizeof(int));
    ASSERT_TRUE(pool_idx != nullptr);
    pools = (memkind_t*)calloc(npools * nthreads, sizeof(memkind_t));
    ASSERT_TRUE(pools != nullptr);

    for (int t = 0; t < nthreads; t++) {
        pool_idx[t] = npools * t;
        status = pthread_create(&threads[t], nullptr, thread_func, &pool_idx[t]);
        ASSERT_EQ(0, status);
    }

    for (int t = 0; t < nthreads; t++) {
        status = pthread_join(threads[t], nullptr);
        ASSERT_EQ(0, status);
    }

    free(pools);
    free(threads);
    free(pool_idx);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemDestroyKind)
{
    const size_t pmem_array_size = 10;
    struct memkind *pmem_kind_array[pmem_array_size] = {nullptr};

    int err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE,
                                  &pmem_kind_array[0]);
    EXPECT_EQ(err, 0);

    err = memkind_destroy_kind(pmem_kind_array[0]);
    EXPECT_EQ(err, 0);

    for (unsigned int i = 0; i < pmem_array_size; ++i) {
        err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE, &pmem_kind_array[i]);
        EXPECT_EQ(err, 0);
    }

    char *pmem_middle_name = pmem_kind_array[5]->name;
    err = memkind_destroy_kind(pmem_kind_array[5]);
    EXPECT_EQ(err, 0);

    err = memkind_destroy_kind(pmem_kind_array[6]);
    EXPECT_EQ(err, 0);

    err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE, &pmem_kind_array[5]);
    EXPECT_EQ(err, 0);

    char *pmem_new_middle_name = pmem_kind_array[5]->name;

    EXPECT_STREQ(pmem_middle_name, pmem_new_middle_name);

    for (unsigned int i = 0; i < pmem_array_size; ++i) {
        if (i != 6) {
            err = memkind_destroy_kind(pmem_kind_array[i]);
            EXPECT_EQ(err, 0);
        }
    }
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemDestroyKindArenaZero)
{
    struct memkind *pmem_temp_1 = nullptr;
    struct memkind *pmem_temp_2 = nullptr;
    unsigned int arena_zero = 0;
    int err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE, &pmem_temp_1);
    EXPECT_EQ(err, 0);

    arena_zero = pmem_temp_1->arena_zero;
    err = memkind_destroy_kind(pmem_temp_1);
    EXPECT_EQ(err, 0);
    err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE, &pmem_temp_2);
    EXPECT_EQ(err, 0);

    EXPECT_EQ(arena_zero,pmem_temp_2->arena_zero);

    err = memkind_destroy_kind(pmem_temp_2);
    EXPECT_EQ(err, 0);
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemCreateDestroyKindLoop)
{
    struct memkind *pmem_temp = nullptr;

    for (unsigned int i = 0; i < MEMKIND_MAX_KIND; ++i) {
        int err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE, &pmem_temp);
        EXPECT_EQ(err, 0);
        err = memkind_destroy_kind(pmem_temp);
        EXPECT_EQ(err, 0);
    }
}

TEST_F(MemkindPmemTests,
       test_TC_MEMKIND_PmemCreateDestroyKindLoopWithMallocSmallSize)
{
    struct memkind *pmem_temp = nullptr;
    const size_t size = 1024;

    for (unsigned int i = 0; i < MEMKIND_MAX_KIND; ++i) {
        int err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE, &pmem_temp);
        EXPECT_EQ(err, 0);
        void *ptr = memkind_malloc(pmem_temp, size);
        EXPECT_TRUE(nullptr != ptr);
        memkind_free(pmem_temp, ptr);
        err = memkind_destroy_kind(pmem_temp);
        EXPECT_EQ(err, 0);
    }
}

TEST_F(MemkindPmemTests,
       test_TC_MEMKIND_PmemCreateDestroyKindLoopWithMallocChunkSize)
{
    struct memkind *pmem_temp = nullptr;
    const size_t size = MEMKIND_PMEM_CHUNK_SIZE;

    for (unsigned int i = 0; i < MEMKIND_MAX_KIND; ++i) {
        int err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE, &pmem_temp);
        EXPECT_EQ(err, 0);
        void *ptr = memkind_malloc(pmem_temp, size);
        EXPECT_TRUE(nullptr != ptr);
        memkind_free(pmem_temp, ptr);
        err = memkind_destroy_kind(pmem_temp);
        EXPECT_EQ(err, 0);
    }
}

TEST_F(MemkindPmemTests,
       test_TC_MEMKIND_PmemCreateDestroyKindLoopWithRealloc)
{
    struct memkind *pmem_temp = nullptr;
    const size_t size_1 = 512;
    const size_t size_2 = 1024;

    for (unsigned int i = 0; i < MEMKIND_MAX_KIND; ++i) {
        int err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE, &pmem_temp);
        EXPECT_EQ(err, 0);
        void *ptr = memkind_malloc(pmem_temp, size_1);
        EXPECT_TRUE(nullptr != ptr);
        void *ptr_2 = memkind_realloc(pmem_temp, ptr, size_2);
        EXPECT_TRUE(nullptr != ptr_2);
        memkind_free(pmem_temp, ptr_2);
        err = memkind_destroy_kind(pmem_temp);
        EXPECT_EQ(err, 0);
    }
}

TEST_F(MemkindPmemTests,
       test_TC_MEMKIND_PmemCreateCheckErrorCodeArenaCreate)
{
    struct memkind *pmem_temp[MEMKIND_MAX_KIND] = { nullptr };
    unsigned i = 0, j = 0;
    int err = 0;

    for (i = 0; i < MEMKIND_MAX_KIND; ++i) {
        err = memkind_create_pmem(PMEM_DIR, MEMKIND_PMEM_MIN_SIZE, &pmem_temp[i]);
        if (err) {
            EXPECT_EQ(err, MEMKIND_ERROR_ARENAS_CREATE);
            break;
        }
    }
    for (j = 0; j < i; ++j) {
        err = memkind_destroy_kind(pmem_temp[j]);
        EXPECT_EQ(err, 0);
    }
}

static void* thread_func_kinds(void* arg)
{
    memkind_t pmem_thread_kind;
    int err = 0;

    pthread_mutex_lock(&mutex);
    pthread_cond_wait(&cond, &mutex);
    pthread_mutex_unlock(&mutex);

    err = memkind_create_pmem(PMEM_DIR, PMEM_PART_SIZE, &pmem_thread_kind);

    if(err == 0) {
        void *test = memkind_malloc(pmem_thread_kind, 32);
        EXPECT_TRUE(test != nullptr);

        memkind_free(pmem_thread_kind, test);
        err = memkind_destroy_kind(pmem_thread_kind);
        EXPECT_EQ(0, err);
    }

    return nullptr;
}

TEST_F(MemkindPmemTests, test_TC_MEMKIND_PmemMultithreadsStressKindsCreate)
{
    const int nthreads = 50;
    int i, t, err;
    int max_possible_kind = 0;
    memkind_t pmem_kinds[MEMKIND_MAX_KIND] = {nullptr};
    pthread_t *threads = (pthread_t*)calloc(nthreads, sizeof(pthread_t));
    ASSERT_TRUE(threads != nullptr);

    // This loop will create as many kinds as possible
    // to obtain a real kind limit
    for (i = 0; i < MEMKIND_MAX_KIND; i++) {
        err = memkind_create_pmem(PMEM_DIR, PMEM_PART_SIZE, &pmem_kinds[i]);
        if(err != 0) {
            ASSERT_TRUE(i > 0);
            max_possible_kind = i;
            --i;
            break;
        }
        ASSERT_TRUE(nullptr != pmem_kinds[i]);
    }

    // destroy last kind so it will be possible
    // to create only one pmem kind in threads
    err = memkind_destroy_kind(pmem_kinds[i]);
    ASSERT_EQ(0, err);

    for (t = 0; t < nthreads; t++) {
        err = pthread_create(&threads[t], nullptr, thread_func_kinds, nullptr);
        ASSERT_EQ(0, err);
    }

    sleep(1);
    pthread_cond_broadcast(&cond);

    for (t = 0; t < nthreads; t++) {
        err = pthread_join(threads[t], nullptr);
        ASSERT_EQ(0, err);
    }

    for (i = 0; i < max_possible_kind - 1; i++) {
        err = memkind_destroy_kind(pmem_kinds[i]);
        ASSERT_EQ(0, err);
    }

    free(threads);
}
