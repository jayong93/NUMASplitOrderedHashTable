#ifndef CDC7572F_E1AD_4B7D_B182_4CA81AA68BB4
#define CDC7572F_E1AD_4B7D_B182_4CA81AA68BB4

#include <mutex>
#include <thread>
#include <iostream>
#include <chrono>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <climits>
#include <algorithm>
#include <optional>

using namespace std;

constexpr unsigned MAX_THREAD = 32;
constexpr uintptr_t WITH_MARK = -1;
constexpr uintptr_t POINTER_ONLY = -2;

class LFNODE
{
public:
    unsigned long key;
    unsigned long value;
    LFNODE *next;

    LFNODE(unsigned long key, unsigned long value) : key{key}, next{nullptr}, value{value} {}

    LFNODE *GetNext()
    {
        return reinterpret_cast<LFNODE *>((uintptr_t)next & POINTER_ONLY);
    }

    void SetNext(LFNODE *ptr)
    {
        next = ptr;
    }

    LFNODE *GetNextWithMark(bool *mark)
    {
        uintptr_t temp = (uintptr_t)next;
        *mark = temp & 1;
        return reinterpret_cast<LFNODE *>(temp & POINTER_ONLY);
    }

    bool CAS(uintptr_t old_value, uintptr_t new_value)
    {
        return atomic_compare_exchange_strong(
            reinterpret_cast<atomic_uintptr_t *>(&next),
            &old_value, new_value);
    }

    bool CAS(LFNODE *old_next, LFNODE *new_next, bool old_mark, bool new_mark)
    {
        uintptr_t old_value = reinterpret_cast<uintptr_t>(old_next);
        if (old_mark)
            old_value = old_value | 0x1;
        else
            old_value = old_value & POINTER_ONLY;
        uintptr_t new_value = reinterpret_cast<uintptr_t>(new_next);
        if (new_mark)
            new_value = new_value | 0x1;
        else
            new_value = new_value & POINTER_ONLY;
        return CAS(old_value, new_value);
    }

    bool TryMark(LFNODE *ptr)
    {
        uintptr_t old_value = reinterpret_cast<uintptr_t>(ptr) & POINTER_ONLY;
        uintptr_t new_value = old_value | 1;
        return CAS(old_value, new_value);
    }

    bool IsMarked()
    {
        return (0 != ((uintptr_t)next & 1));
    }
};

struct EpochNode
{
    LFNODE *ptr;
    unsigned long long epoch;

    EpochNode(LFNODE *ptr, unsigned long long epoch) : ptr{ptr}, epoch{epoch} {}
};

#ifdef LFSET_IMPL
#define EXTERN
EXTERN atomic_ullong tid_counter;
thread_local unsigned tid = tid_counter.fetch_add(1, memory_order_relaxed);
#else
#define EXTERN extern
EXTERN atomic_ullong tid_counter;
extern thread_local unsigned tid;
#endif

EXTERN atomic_ullong g_epoch;
EXTERN atomic_ullong *t_epochs[MAX_THREAD];
EXTERN thread_local vector<EpochNode> retired_list;
EXTERN thread_local unsigned counter;
constexpr unsigned epoch_freq = 20;
constexpr unsigned empty_freq = 10;

void retire(LFNODE *node);
void start_op();
void end_op();

class LFSET
{
    LFNODE head;

public:
    LFSET() : head{0, 0} {}
    void Init()
    {
        while (head.GetNext() != nullptr)
        {
            LFNODE *temp = head.GetNext();
            head.next = temp->next;
            delete temp;
        }
    }

    void Dump()
    {
        LFNODE *ptr = head.GetNext();
        cout << "Result Contains : ";
        for (int i = 0; i < 20; ++i)
        {
            if (nullptr == ptr)
                break;
            cout << ptr->key << ", ";
            ptr = ptr->GetNext();
        }
        cout << endl;
    }

    bool Find(unsigned long x, LFNODE **pred, LFNODE **curr)
    {
        start_op();
    retry:
        *pred = &head;
        *curr = (*pred)->GetNext();
        while (true)
        {
            if (*curr == nullptr)
                return false;
            bool removed;
            LFNODE *su = (*curr)->GetNextWithMark(&removed);
            if (true == removed)
            {
                if (false == (*pred)->CAS(*curr, su, false, false))
                    goto retry;
                retire(*curr);
            }
            else if ((*curr)->key >= x)
            {
                return ((*curr)->key == x);
            }
            else
            {
                *pred = *curr;
            }
            *curr = (*curr)->GetNext();
        }
    }
    // 성공하면 삽입된 노드 pointer 반환, 실패하면 이미 삽입된 노드의 pointer 반환
    LFNODE *Add(unsigned long x, unsigned long value = 0)
    {
        LFNODE *pred, *curr;
        LFNODE *e = new LFNODE(x, value);
        while (true)
        {
            if (true == Find(x, &pred, &curr))
            {
                end_op();
                delete e;
                return curr;
            }
            else
            {
                e->SetNext(curr);
                if (false == pred->CAS(curr, e, false, false))
                {
                    end_op();
                    continue;
                }
                end_op();
                return e;
            }
        }
    }
    bool Add(LFNODE &node)
    {
        LFNODE *pred, *curr;
        while (true)
        {
            if (true == Find(node.key, &pred, &curr))
            {
                end_op();
                return false;
            }
            else
            {
                node.SetNext(curr);
                if (false == pred->CAS(curr, &node, false, false))
                {
                    end_op();
                    continue;
                }
                end_op();
                return true;
            }
        }
    }
    bool Remove(unsigned long x)
    {
        LFNODE *pred, *curr;
        while (true)
        {
            if (false == Find(x, &pred, &curr)) {
                end_op();
                return false;
            }
            else
            {
                LFNODE *succ = curr->GetNext();
                if (false == curr->TryMark(succ))
                {
                    end_op();
                    continue;
                }
                if (true == pred->CAS(curr, succ, false, false))
                {
                    retire(curr);
                }
                end_op();
                return true;
            }
        }
    }
    optional<unsigned long> Contains(unsigned long x)
    {
        start_op();
        optional<unsigned long> ret;
        LFNODE *curr = head.GetNext();
        while (curr != nullptr && curr->key < x)
        {
            curr = curr->GetNext();
        }

        if (curr != nullptr && (false == curr->IsMarked()) && (x == curr->key))
        {
            ret = curr->value;
        }
        end_op();
        return ret;
    }
    optional<unsigned long> Contains(LFNODE &bucket, unsigned long x)
    {
        start_op();
        optional<unsigned long> ret;
        LFNODE *curr = &bucket;
        while (curr != nullptr && curr->key < x)
        {
            curr = curr->GetNext();
        }

        if (curr != nullptr && (false == curr->IsMarked()) && (x == curr->key))
        {
            ret = curr->value;
        }
        end_op();
        return ret;
    }
};
#endif /* CDC7572F_E1AD_4B7D_B182_4CA81AA68BB4 */
