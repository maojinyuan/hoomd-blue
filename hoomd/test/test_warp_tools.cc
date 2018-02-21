// Copyright (c) 2009-2018 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

// Maintainer: mphoward

/*!
 * \file test_warp_tools.cc
 * \brief Tests for warp-level primitives.
 */

#include "hoomd/ExecutionConfiguration.h"
#include "hoomd/GPUArray.h"
#include "test_warp_tools.cuh"

#include "upp11_config.h"
HOOMD_UP_MAIN();

//! Runs the warp scan tests using different number of threads per row.
/*!
 * \param tpp Number of threads to use per row of data.
 */
void test_warp_scan(const unsigned int tpp)
    {
    auto exec_conf = std::make_shared<ExecutionConfiguration>(ExecutionConfiguration::GPU);

    // make a N x width array filled with data
    const unsigned int N = 2;
    const unsigned int width = 5;
    GPUArray<int> vec(N*width, exec_conf);
        {
        ArrayHandle<int> h_vec(vec, access_location::host, access_mode::overwrite);

        // first row
        h_vec.data[0] = 1;
        h_vec.data[1] = 2;
        h_vec.data[2] = 3;
        h_vec.data[3] = 4;
        h_vec.data[4] = 5;

        // second row
        h_vec.data[5] = 0;
        h_vec.data[6] = 1;
        h_vec.data[7] = 1;
        h_vec.data[8] = 0;
        h_vec.data[9] = 0;
        }

    const unsigned int niter = (width + tpp - 1) / tpp;
    Index3D scan_idx(N, tpp + 1 /* holds sum */, niter);
    GPUArray<int> scan(scan_idx.getNumElements(), exec_conf);
    GPUArray<int> sum(N, exec_conf);
        {
        ArrayHandle<int> d_vec(vec, access_location::device, access_mode::readwrite);
        ArrayHandle<int> d_scan(scan, access_location::device, access_mode::overwrite);
        ArrayHandle<int> d_sum(sum, access_location::device, access_mode::overwrite);
        scan_params params(d_vec.data, d_scan.data, d_sum.data, N, width, tpp, scan_idx);
        warp_scan(params);
        }

    // sums should always be the same regardless of the number of threads in the scan
        {
        ArrayHandle<int> h_sum(sum, access_location::host, access_mode::read);
        UP_ASSERT_EQUAL(h_sum.data[0], 15);
        UP_ASSERT_EQUAL(h_sum.data[1], 2);
        }

    // test scan output, which depends on tpp
        {
        ArrayHandle<int> h_vec(vec, access_location::host, access_mode::read);
        ArrayHandle<int> h_scan(scan, access_location::host, access_mode::read);

        // loop for each row
        for (unsigned int i = 0; i < N; ++i)
            {
            unsigned int cntr = 0;
            // loop through each iteration of the scan that is expected
            for (unsigned int iter = 0; iter < niter; ++iter)
                {
                // exclusive prefix sum on tpp entries
                int sum = 0;
                for (unsigned int tid = 0; tid < tpp; ++tid)
                    {
                    UP_ASSERT_EQUAL(h_scan.data[scan_idx(i, tid, iter)], sum);
                    if (cntr < width)
                        sum += h_vec.data[i * width + cntr];
                    ++cntr;
                    }
                UP_ASSERT_EQUAL(h_scan.data[scan_idx(i, tpp, iter)], sum);
                }
            }
        }
    }

//! Warp scan with 1 thread
UP_TEST( test_warp_scan_1 )
    {
    test_warp_scan(1);
    }
//! Warp scan with 2 threads
UP_TEST( test_warp_scan_2 )
    {
    test_warp_scan(2);
    }
//! Warp scan with 4 threads
UP_TEST( test_warp_scan_4 )
    {
    test_warp_scan(4);
    }
//! Warp scan with 8 threads
UP_TEST( test_warp_scan_8 )
    {
    test_warp_scan(8);
    }
//! Warp scan with 16 threads
UP_TEST( test_warp_scan_16 )
    {
    test_warp_scan(16);
    }
//! Warp scan with 32 threads
UP_TEST( test_warp_scan_32 )
    {
    test_warp_scan(32);
    }
