#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <tbb/concurrent_hash_map.h>
#include "split_ordered.h"
static const int NUM_TEST = 4'000'000;
//static const int RANGE = 1'000;
constexpr unsigned MAX_THREAD = 64;

#ifndef WRITE_RATIO
#define WRITE_RATIO 30
#endif

using namespace std;
using namespace chrono;

tbb::concurrent_hash_map<unsigned long, unsigned long> my_table;

unsigned long fast_rand(void)
{ //period 2^96-1
    static thread_local unsigned long x = 123456789, y = 362436069, z = 521288629;
    unsigned long t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

    t = x;
    x = y;
    y = z;
    z = t ^ x ^ y;

    return z;
}



void benchmark(int num_thread)
{
    pin_thread();
    for (int i = 0; i < NUM_TEST / num_thread; ++i)
    {
        if (fast_rand() % 100 < WRITE_RATIO) {
            if (fast_rand() % 100 < 50) {
                tbb::concurrent_hash_map<unsigned long, unsigned long>::accessor a;
                if (my_table.insert(a, fast_rand())) {
                    a->second = fast_rand();
                }
            }
            else {
                my_table.erase(fast_rand());
            }
        }
        else {
            tbb::concurrent_hash_map<unsigned long, unsigned long>::const_accessor a;
            my_table.find(a, fast_rand());
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "you have to give a thread num\n");
        exit(-1);
    }
    unsigned num_thread = atoi(argv[1]);
    if (MAX_THREAD < num_thread)
    {
        fprintf(stderr, "the upper limit of a number of thread is %d\n", MAX_THREAD);
        exit(-1);
    }

    vector<thread> worker;
    auto start_t = high_resolution_clock::now();
    for (int i = 0; i < num_thread; ++i)
        worker.push_back(thread{benchmark, num_thread});
    for (auto &th : worker)
        th.join();
    auto du = high_resolution_clock::now() - start_t;

    cout << num_thread << " Threads,  Time = ";
    cout << duration_cast<milliseconds>(du).count() << " ms" << endl;
}
