#define LFSET_IMPL
#include "lf_set.h"

void retire(LFNODE *node)
{
    retired_list.emplace_back(node, g_epoch.load(memory_order_relaxed));
    ++counter;
    if (counter % epoch_freq == 0)
    {
        g_epoch.fetch_add(1, memory_order_relaxed);
    }
    if (counter % empty_freq == 0)
    {
        auto min_epoch = ULLONG_MAX;
        for (auto &epoch : t_epochs)
        {
            auto e = epoch->load(memory_order_acquire);
            if (min_epoch > e)
            {
                min_epoch = e;
            }
        }

        auto removed_it = remove_if(retired_list.begin(), retired_list.end(), [min_epoch](auto &r_node) {
            if (r_node.epoch < min_epoch)
            {
                delete r_node.ptr;
                return true;
            }
            return false;
        });
        retired_list.erase(removed_it, retired_list.end());
    }
}

void start_op()
{
    t_epochs[tid % MAX_THREAD]->store(g_epoch.load(memory_order_relaxed), memory_order_release);
}

void end_op()
{
    t_epochs[tid % MAX_THREAD]->store(ULLONG_MAX, memory_order_release);
}