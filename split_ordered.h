#ifndef ADDE381D_44C2_4BEC_A967_FE5043D7D5B2
#define ADDE381D_44C2_4BEC_A967_FE5043D7D5B2

#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "lf_set.h"
#include "SPSCQueue.h"

constexpr unsigned SEGMENT_SIZE = 1024 * 1024;
constexpr unsigned LOAD_FACTOR = 1;

template <typename T>
using Segments = std::array<T, SEGMENT_SIZE>;

struct BucketArray
{
    BucketArray(LFNODE *first_bucket);
    std::array<std::atomic<Segments<LFNODE *> *>, SEGMENT_SIZE> segments;
    LFNODE *get_bucket(uintptr_t bucket);
    void set_bucket(uintptr_t bucket, LFNODE *head);
};

struct BucketNotification
{
    uintptr_t bucket_idx;
    LFNODE *dummy_node;
};

class SO_Hashtable
{
public:
    SO_Hashtable();
    bool remove(unsigned long key);
    optional<unsigned long> find(unsigned long key);
    bool insert(unsigned long key, unsigned long value);

private:
    atomic_uintptr_t bucket_num{2};
    atomic_uintptr_t item_num{0};
    LFSET item_set;
    std::vector<std::unique_ptr<BucketArray>> bucket_array;
    std::vector<std::unique_ptr<SPSCQueue<BucketNotification>>> msg_queues;

    LFNODE *init_bucket(uintptr_t bucket);
};

#endif /* ADDE381D_44C2_4BEC_A967_FE5043D7D5B2 */
