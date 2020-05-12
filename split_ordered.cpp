#include "split_ordered.h"

using namespace std;

unsigned long so_dummy_key(unsigned long) {

}

unsigned long so_regular_key(unsigned long) {

}

uintptr_t get_parent(uintptr_t bucket)
{
    auto mask = 1 << (UINTPTR_WIDTH - 1);
    for (auto i = 0; i < UINTPTR_WIDTH; ++i)
    {
        if (bucket & mask != 0)
        {
            return bucket & ~mask;
        }
        mask >>= 1;
    }
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

void SO_Hashtable::init_bucket(uintptr_t bucket)
{
    auto parent = get_parent(bucket);
    if (this->bucket_array->get_bucket(parent) == nullptr) {
        this->init_bucket(parent);
    }
    auto dummy = item_set.Add(so_dummy_key(bucket));
    this->bucket_array->set_bucket(bucket, dummy);
}

bool SO_Hashtable::remove(unsigned long key)
{
}

optional<unsigned long> SO_Hashtable::find(unsigned long key)
{
    auto bucket = key % this->bucket_num.load(memory_order_relaxed);
    if (this->bucket_array->get_bucket(bucket) == nullptr) {
        this->init_bucket(bucket);
    }
    return this->item_set.Contains(*this->bucket_array->get_bucket(bucket), so_regular_key(key));
}

bool SO_Hashtable::insert(unsigned long key, unsigned long value)
{
    auto node = new LFNODE{so_regular_key(key), value};
    auto bucket = key % this->bucket_num.load(memory_order_relaxed);
    if (this->bucket_array->get_bucket(bucket) == nullptr) {
        this->init_bucket(bucket);
    }
    if (!this->item_set.Add(*node)) {
        delete node;
        return false;
    }

    auto curr_size = this->bucket_num.load(memory_order_relaxed);
    if (this->item_num.fetch_add(1, memory_order_relaxed) + 1 / curr_size > LOAD_FACTOR) {
        this->bucket_num.compare_exchange_strong(curr_size, curr_size * 2);
    }
    return true;
}
