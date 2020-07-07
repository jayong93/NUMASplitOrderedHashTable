#include <thread>
#include "lf_set.h"
#include "split_ordered.h"

static const unsigned NUMA_NODE_NUM = numa_num_configured_nodes();
static const unsigned CPU_NUM = numa_num_configured_cpus()/2;
static const unsigned CORE_PER_NODE = CPU_NUM/NUMA_NODE_NUM;

static atomic_uint tid_counter{0};
static thread_local unsigned tid = tid_counter.fetch_add(1, memory_order_relaxed);
static thread_local const unsigned numa_id = (tid/CORE_PER_NODE) % NUMA_NODE_NUM;

void pin_thread()
{
    if (-1 == numa_run_on_node(numa_id))
    {
        fprintf(stderr, "Can't bind thread #%d to node #%d\n", tid, numa_id);
        exit(-1);
    }
}
