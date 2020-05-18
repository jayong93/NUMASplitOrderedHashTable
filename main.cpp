#include <chrono>
#include "lf_set.h"
#include "split_ordered.h"
static const int NUM_TEST = 4'000'000;
static const int RANGE = 1'000;

using namespace std;
using namespace chrono;

SO_Hashtable my_table;

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
        //	if (0 == i % 100000) cout << ".";
        switch (fast_rand() % 3)
        {
        case 0:
            my_table.insert(fast_rand() % RANGE, fast_rand());
            break;
        case 1:
            my_table.remove(fast_rand() % RANGE);
            break;
        case 2:
            my_table.find(fast_rand() % RANGE);
            break;
        default:
            cout << "ERROR!!!\n";
            exit(-1);
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
