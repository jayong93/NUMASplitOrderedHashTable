#include <numa.h>
#include "split_ordered.h"

namespace so_hash_table {

template<typename T>
constexpr int width() {
    return sizeof(T) * 8;
}

using namespace std;

static const unsigned NUMA_NODE_NUM = numa_num_configured_nodes();
static const unsigned CPU_NUM = numa_num_configured_cpus()/2;
static const unsigned CORE_PER_NODE = CPU_NUM/NUMA_NODE_NUM;

// macros to generate the lookup table (at compile-time)
#define R2(n) n, n + 2 * 64, n + 1 * 64, n + 3 * 64
#define R4(n) R2(n), R2(n + 2 * 16), R2(n + 1 * 16), R2(n + 3 * 16)
#define R6(n) R4(n), R4(n + 2 * 4), R4(n + 1 * 4), R4(n + 3 * 4)
#define REVERSE_BITS R6(0), R6(2), R6(1), R6(3)

// lookup-table to store the reverse of each index of the table
// The macro REVERSE_BITS generates the table
static unsigned long lookup[256] = {REVERSE_BITS};
static atomic_uint tid_counter{0};
static thread_local unsigned tid = tid_counter.fetch_add(1, memory_order_relaxed);
static thread_local const unsigned numa_id = (tid/CORE_PER_NODE) % NUMA_NODE_NUM;

unsigned long reverse_bits(unsigned long num)
{
    unsigned long result = 0;
    for (auto i = 0; i < width<unsigned long>() / 8; ++i)
    {
        result |= lookup[(num >> (i * 8)) & 0xff] << ((width<unsigned long>() / 8 - 1 - i) * 8);
    }
    return result;
}

unsigned long so_regular_key(unsigned long key)
{
    return reverse_bits(key | (1ul << (width<unsigned long>() - 1)));
}

unsigned long so_dummy_key(unsigned long key)
{
    return reverse_bits(key);
}

uintptr_t get_parent(uintptr_t bucket)
{
    auto mask = 1ul << (width<uintptr_t>() - 1);
    for (auto i = 0; i < width<uintptr_t>(); ++i)
    {
        if ((bucket & mask) != 0)
        {
            return bucket & ~mask;
        }
        mask >>= 1;
    }
    return 0;
}

LFNODE *BucketArray::get_bucket(uintptr_t bucket)
{
    auto segment = bucket / SEGMENT_SIZE;
    auto seg_ptr = this->segments[segment].load(memory_order_relaxed);
    if (seg_ptr == nullptr)
    {
        return nullptr;
    }
    auto remain = bucket % SEGMENT_SIZE;
    return (*seg_ptr)[remain];
}

void BucketArray::set_bucket(uintptr_t bucket, LFNODE *head)
{
    auto segment = bucket / SEGMENT_SIZE;
    auto &atomic_seg_ptr = this->segments[segment];
    if (atomic_seg_ptr.load(memory_order_relaxed) == nullptr)
    {
        auto new_seg = new array<LFNODE *, SEGMENT_SIZE>;
        for (auto &seg : *new_seg)
        {
            seg = nullptr;
        }

        array<LFNODE *, SEGMENT_SIZE> *original = nullptr;
        if (!atomic_seg_ptr.compare_exchange_strong(original, new_seg))
        {
            delete new_seg;
        }
    }

    auto seg_ptr = atomic_seg_ptr.load(memory_order_relaxed);
    (*seg_ptr)[bucket % SEGMENT_SIZE] = head;
}

LFNODE* SO_Hashtable::init_bucket(uintptr_t bucket)
{
    auto parent = get_parent(bucket);
    auto parent_node = this->bucket_array->get_bucket(parent);
    if (parent_node == nullptr)
    {
        parent_node = this->init_bucket(parent);
    }
    auto dummy = item_set.Add(*parent_node, so_dummy_key(bucket));
    this->bucket_array->set_bucket(bucket, dummy);
    return dummy;
}

bool SO_Hashtable::remove(unsigned long key)
{
    auto bucket = key % this->bucket_num.load(memory_order_relaxed);
    auto bucket_node = this->bucket_array->get_bucket(bucket);
    if (bucket_node == nullptr)
    {
        bucket_node = this->init_bucket(bucket);
    }
    if (false == this->item_set.Remove(*bucket_node, so_regular_key(key)))
        return false;

    this->item_num.fetch_sub(1, memory_order_relaxed);
    return true;
}

optional<unsigned long> SO_Hashtable::find(unsigned long key)
{
    auto bucket = key % this->bucket_num.load(memory_order_relaxed);
    auto bucket_node = this->bucket_array->get_bucket(bucket);
    if (bucket_node == nullptr)
    {
        bucket_node = this->init_bucket(bucket);
    }
    return this->item_set.Contains(*this->bucket_array->get_bucket(bucket), so_regular_key(key));
}

bool SO_Hashtable::insert(unsigned long key, unsigned long value)
{
    auto node = new LFNODE{so_regular_key(key), value};
    auto bucket = key % this->bucket_num.load(memory_order_relaxed);
    auto bucket_node = this->bucket_array->get_bucket(bucket);
    if (bucket_node == nullptr)
    {
        bucket_node = this->init_bucket(bucket);
    }
    if (!this->item_set.Add(*bucket_node, *node))
    {
        delete node;
        return false;
    }

    auto curr_size = this->bucket_num.load(memory_order_relaxed);
    if ((this->item_num.fetch_add(1, memory_order_relaxed) + 1) / curr_size >= LOAD_FACTOR)
    {
        this->bucket_num.compare_exchange_strong(curr_size, curr_size * 2);
    }
    return true;
}

SO_Hashtable::SO_Hashtable() : bucket_num{2}, item_num{0}
{
    LFNODE *first_bucket = new LFNODE{0, 0};
    item_set.Add(item_set.get_head(), *first_bucket);
    bucket_array.reset(new BucketArray{first_bucket});
}

BucketArray::BucketArray(LFNODE *first_bucket)
{
    auto first_arr = new array<LFNODE *, SEGMENT_SIZE>;
    (*first_arr)[0] = first_bucket;
    segments[0].store(first_arr, memory_order_relaxed);
}

void pin_thread() {
	if (-1 == numa_run_on_node(numa_id))
    {
        fprintf(stderr, "Can't bind thread #%d to node #%d\n", tid, numa_id);
        exit(-1);
    }
}

}
