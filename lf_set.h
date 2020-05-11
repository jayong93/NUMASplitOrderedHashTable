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

using namespace std;

constexpr unsigned MAX_THREAD = 32;
constexpr uintptr_t WITH_MARK = -1;
constexpr uintptr_t POINTER_ONLY = -2;

class LFNODE
{
public:
    int key;
    unsigned long value;
    LFNODE *next;

    LFNODE(int key, unsigned long value) : key{key}, next{nullptr}, value{value} {}

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
    LFNODE head, tail;

public:
    LFSET() : head{(int)0x80000000, 0}, tail{0x7FFFFFFF, 0}
    {
        head.SetNext(&tail);
    }
    void Init()
    {
        while (head.GetNext() != &tail)
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
            cout << ptr->key << ", ";
            if (&tail == ptr)
                break;
            ptr = ptr->GetNext();
        }
        cout << endl;
    }

    void Find(int x, LFNODE **pred, LFNODE **curr)
    {
        start_op();
    retry:
        LFNODE *pr = &head;
        LFNODE *cu = pr->GetNext();
        while (true)
        {
            bool removed;
            LFNODE *su = cu->GetNextWithMark(&removed);
            while (true == removed)
            {
                if (false == pr->CAS(cu, su, false, false))
                    goto retry;
                retire(cu);
                cu = su;
                su = cu->GetNextWithMark(&removed);
            }
            if (cu->key >= x)
            {
                *pred = pr;
                *curr = cu;
                return;
            }
            pr = cu;
            cu = cu->GetNext();
        }
    }
    bool Add(int x, unsigned long value = 0)
    {
        LFNODE *pred, *curr;
        while (true)
        {
            Find(x, &pred, &curr);

            if (curr->key == x)
            {
                end_op();
                return false;
            }
            else
            {
                LFNODE *e = new LFNODE(x, value);
                e->SetNext(curr);
                if (false == pred->CAS(curr, e, false, false))
                {
                    end_op();
                    continue;
                }
                end_op();
                return true;
            }
        }
    }
    bool Remove(int x)
    {
        LFNODE *pred, *curr;
        while (true)
        {
            Find(x, &pred, &curr);

            if (curr->key != x)
            {
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
                // delete curr;
                end_op();
                return true;
            }
        }
    }
    bool Contains(int x)
    {
        start_op();
        LFNODE *curr = &head;
        while (curr->key < x)
        {
            curr = curr->GetNext();
        }

        auto ret = (false == curr->IsMarked()) && (x == curr->key);
        end_op();
        return ret;
    }
};