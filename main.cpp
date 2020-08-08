#include <random>
#include <chrono>
#include "lf_set.h"
#include "split_ordered.h"
#include "rand_seeds.h"
static const int NUM_TEST = 4'000'000;

#ifndef WRITE_RATIO
#define WRITE_RATIO 30
#endif

using namespace std;
using namespace chrono;

SO_Hashtable my_table;

void benchmark(int num_thread, int tid)
{
	mt19937_64 rng{rand_seeds[tid]};
#ifdef RANGE_LIMIT
    uniform_int_distribution<unsigned long> dist{0, RANGE_LIMIT-1};
#else
    uniform_int_distribution<unsigned long> dist;
#endif
    uniform_int_distribution<unsigned long> cmd_dist{0, 99};

    pin_thread();
    for (int i = 0; i < NUM_TEST / num_thread; ++i)
    {
		if (cmd_dist(rng) < WRITE_RATIO) {
            if (cmd_dist(rng) < 50) {
                my_table.insert(dist(rng), dist(rng));
            }
            else {
                my_table.remove(dist(rng));
            }
        }
        else {
            my_table.find(dist(rng));
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
        worker.push_back(thread{benchmark, num_thread, i});
    for (auto &th : worker)
        th.join();
    auto du = high_resolution_clock::now() - start_t;

    cout << num_thread << " Threads,  Time = ";
    cout << duration_cast<milliseconds>(du).count() << " ms" << endl;
}
