#include <chrono>
#include "lf_set.h"
#include "split_ordered.h"
static const int NUM_TEST = 4'000'000;
//static const int RANGE = 1'000;

#ifndef WRITE_RATIO
#define WRITE_RATIO 30
#endif

using namespace std;
using namespace chrono;

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

void benchmark(SO_Hashtable& my_table, int num_thread)
{
    pin_thread();
    for (int i = 0; i < NUM_TEST / num_thread; ++i)
    {
        if (fast_rand() % 100 < WRITE_RATIO) {
            if (fast_rand() % 100 < 50) {
                my_table.insert(fast_rand(), fast_rand());
            }
            else {
                my_table.remove(fast_rand());
            }
        }
        else {
            my_table.find(fast_rand());
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

    const unsigned NUMA_NODE_NUM = numa_num_configured_nodes();
    const unsigned CPU_NUM = numa_num_configured_cpus()/2;
    const unsigned CORE_PER_NODE = CPU_NUM/NUMA_NODE_NUM;
    auto required_node_num = max(1u, min(num_thread/CORE_PER_NODE, NUMA_NODE_NUM));
    auto real_num_thread = num_thread;
    if (num_thread >= CORE_PER_NODE) {
        real_num_thread -= 1 + required_node_num;
    }

    SO_Hashtable my_table{required_node_num};

    vector<thread> worker;
    auto start_t = high_resolution_clock::now();
    for (int i = 0; i < real_num_thread; ++i)
        worker.push_back(thread{benchmark, ref(my_table), real_num_thread});
    for (auto &th : worker)
        th.join();
    auto du = high_resolution_clock::now() - start_t;

    cout << num_thread << " Threads,  Time = ";
    cout << duration_cast<milliseconds>(du).count() << " ms" << endl;
}
