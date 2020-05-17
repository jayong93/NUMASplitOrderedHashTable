#include <thread>
#include "lf_set.h"
#include "split_ordered.h"

static const unsigned NUMA_NODE_NUM = numa_num_configured_nodes();
static const unsigned CPU_NUM = numa_num_configured_cpus();

template <typename T, typename... Vals>
T *NUMA_alloc(unsigned numa_id, Vals &&... val)
{
    void *raw_ptr = numa_alloc_onnode(sizeof(T), numa_id);
    T *ptr = new (raw_ptr) T(forward<Vals>(val)...);
    return ptr;
}

template <typename T>
void NUMA_dealloc(T *ptr)
{
    ptr->~T();
    numa_free(ptr, sizeof(T));
}

template <typename T>
constexpr int width()
{
    return sizeof(T) * 8;
}

using namespace std;

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
constexpr unsigned long KEY_MASK = ((unsigned long)1 << (width<unsigned long>() - 1));

unsigned get_numa_id()
{
    const auto core_num_per_node = CPU_NUM / NUMA_NODE_NUM;
    return (tid / core_num_per_node) % NUMA_NODE_NUM;
}

unsigned long reverse_bits(unsigned long num)
{
    unsigned long result = 0;
    for (unsigned long i = 0; i < sizeof(unsigned long); ++i)
    {
        result |= (lookup[(num >> (i * 8)) & 0xff] << ((sizeof(unsigned long) - i - 1) * 8));
    }
    return result;
}

unsigned long so_regular_key(unsigned long key)
{
    return reverse_bits(key | KEY_MASK);
}

unsigned long so_dummy_key(unsigned long key)
{
    return reverse_bits(key);
}

uintptr_t get_parent(uintptr_t bucket)
{
    auto mask = (unsigned long)1 << (width<uintptr_t>() - 1);
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

LFNODE *SO_Hashtable::init_bucket(uintptr_t bucket)
{
    auto numa_idx = get_numa_id();
    auto parent = get_parent(bucket);
    auto parent_node = this->bucket_array[numa_idx]->get_bucket(parent);
    if (parent_node == nullptr)
    {
        parent_node = this->init_bucket(parent);
    }
    auto dummy = item_set.Add(*parent_node, so_dummy_key(bucket));
    this->bucket_array[numa_idx]->set_bucket(bucket, dummy);
    return dummy;
}

bool SO_Hashtable::remove(unsigned long key)
{
    auto numa_idx = get_numa_id();
    auto bucket = key % this->bucket_nums[numa_idx]->load(memory_order_relaxed);
    auto bucket_node = this->bucket_array[numa_idx]->get_bucket(bucket);
    if (bucket_node == nullptr)
    {
        bucket_node = this->init_bucket(bucket);
    }
    if (false == this->item_set.Remove(*bucket_node, so_regular_key(key)))
        return false;

    return true;
}

optional<unsigned long> SO_Hashtable::find(unsigned long key)
{
    auto numa_idx = get_numa_id();
    auto bucket = key % this->bucket_nums[numa_idx]->load(memory_order_relaxed);
    auto bucket_node = this->bucket_array[numa_idx]->get_bucket(bucket);
    if (bucket_node == nullptr)
    {
        bucket_node = this->init_bucket(bucket);
    }
    return this->item_set.Contains(*this->bucket_array[numa_idx]->get_bucket(bucket), so_regular_key(key));
}

bool SO_Hashtable::insert(unsigned long key, unsigned long value)
{
    auto numa_idx = get_numa_id();
    auto node = new LFNODE{so_regular_key(key), value};
    auto bucket = key % this->bucket_nums[numa_idx]->load(memory_order_relaxed);
    auto bucket_node = this->bucket_array[numa_idx]->get_bucket(bucket);
    if (bucket_node == nullptr)
    {
        bucket_node = this->init_bucket(bucket);
    }
    if (!this->item_set.Add(*bucket_node, *node))
    {
        delete node;
        return false;
    }
    return true;
}

void global_helper_thread_func(LFSET *set, std::vector<SPSCQueue<BucketNotification> *> *queues)
{
    while (true)
    {
        uintptr_t size = 0;
        start_op();
        LFNODE *prev = &set->get_head();
        LFNODE *curr = prev->GetNext();
        while (curr != nullptr)
        {
            if ((curr->key & 0x1) == 0)
            {
                if (curr->is_new)
                {
                    curr->is_new = false;
                    uintptr_t idx = reverse_bits(curr->key);
                    for (auto &queue : *queues)
                    {
                        queue->emplace(idx, curr);
                    }
                }
            } else if (!curr->IsMarked()) {
                size += 1;
            }
            prev = curr;
            curr = curr->GetNext();
        }
        for(auto &queue : *queues) {
            queue->emplace(size, nullptr);
        }
        end_op();
        std::this_thread::sleep_for(1ms);
    }
}

void local_helper_thread_fun(unsigned numa_idx, SPSCQueue<BucketNotification> *queue, BucketArray *bucket_arr, atomic_uintptr_t *bucket_num, atomic_uintptr_t *item_num)
{
    if (-1 == numa_run_on_node(numa_idx))
    {
        fprintf(stderr, "Can't bind local helper thread to node #%d\n", numa_idx);
        exit(-1);
    }
    while (true)
    {
        auto bucket_noti = queue->deq();
        if (!bucket_noti)
        {
            std::this_thread::sleep_for(1ms);
            continue;
        }

        if (bucket_noti->node == nullptr)
        {
            auto new_item_num = bucket_noti->org_key;
            item_num->store(new_item_num, memory_order_relaxed);
            const auto old_bucket_num = bucket_num->load(memory_order_relaxed);
            if (new_item_num / old_bucket_num >= LOAD_FACTOR) {
                bucket_num->store(old_bucket_num * 2);
            }
        }
        else if ((bucket_noti->org_key & KEY_MASK) == 0)
        {
            bucket_arr->set_bucket(bucket_noti->org_key, bucket_noti->node);
        }
    }
}

SO_Hashtable::SO_Hashtable()
{
    const auto numa_node_num = numa_num_configured_nodes();
    LFNODE *first_bucket = new LFNODE{0, 0};
    first_bucket->is_new = false;
    item_set.Add(item_set.get_head(), *first_bucket);
    for (auto i = 0; i < numa_node_num; ++i)
    {
        bucket_array.push_back(NUMA_alloc<BucketArray>(i, first_bucket));
        msg_queues.push_back(NUMA_alloc<SPSCQueue<BucketNotification>>(i));
        bucket_nums.push_back(NUMA_alloc<atomic_uintptr_t>(i, 2));
        item_nums.push_back(NUMA_alloc<atomic_uintptr_t>(i, 0));
    }
    this->global_helper = std::thread{global_helper_thread_func, &this->item_set, &this->msg_queues};
    for (auto i = 0; i < numa_node_num; ++i)
    {
        this->local_helpers.emplace_back(local_helper_thread_fun, i, this->msg_queues[i], bucket_array[i], bucket_nums[i], item_nums[i]);
    }
}

BucketArray::BucketArray(LFNODE *first_bucket)
{
    auto first_arr = new array<LFNODE *, SEGMENT_SIZE>;
    (*first_arr)[0] = first_bucket;
    segments[0].store(first_arr, memory_order_relaxed);
}

SO_Hashtable::~SO_Hashtable()
{
    for (auto i = 0; i < NUMA_NODE_NUM; ++i)
    {
        NUMA_dealloc(bucket_array[i]);
        NUMA_dealloc(bucket_nums[i]);
        NUMA_dealloc(msg_queues[i]);
    }
}

void pin_thread()
{
    if (-1 == numa_run_on_node(get_numa_id()))
    {
        fprintf(stderr, "Can't bind thread #%d to node #%d\n", tid, get_numa_id());
        exit(-1);
    }
}
