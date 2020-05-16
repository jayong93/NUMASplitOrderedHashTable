#include "lf_set.h"

struct EpochNode
{
    LFNODE *ptr;
    unsigned long long epoch;

    EpochNode(LFNODE *ptr, unsigned long long epoch) : ptr{ptr}, epoch{epoch} {}
};

static atomic_ullong g_epoch{0};
static atomic_ullong t_epochs[MAX_THREAD];
thread_local vector<EpochNode> retired_list;
thread_local unsigned counter{0};
constexpr unsigned epoch_freq = 100;
constexpr unsigned empty_freq = 1000;
static atomic_ullong tid_counter{0};
thread_local unsigned tid = tid_counter.fetch_add(1, memory_order_relaxed);

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
            auto e = epoch.load(memory_order_acquire);
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
    t_epochs[tid % MAX_THREAD].store(g_epoch.load(memory_order_relaxed), memory_order_release);
}

void end_op()
{
    t_epochs[tid % MAX_THREAD].store(ULLONG_MAX, memory_order_release);
}

LFSET::LFSET() : head{0, 0}
{
}

void LFSET::Init()
{
    while (head.GetNext() != nullptr)
    {
        LFNODE *temp = head.GetNext();
        head.next = temp->next;
        delete temp;
    }
}

void LFSET::Dump()
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

bool LFSET::Find(LFNODE& from, unsigned long x, LFNODE **pred, LFNODE **curr)
{
    start_op();
retry:
    *pred = &from;
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

LFNODE *LFSET::Add(LFNODE& from, unsigned long x, unsigned long value)
{
    LFNODE *pred, *curr;
    LFNODE *e = new LFNODE(x, value);
    while (true)
    {
        if (true == Find(from, x, &pred, &curr))
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

bool LFSET::Add(LFNODE& from,LFNODE &node)
{
    LFNODE *pred, *curr;
    while (true)
    {
        if (true == Find(from, node.key, &pred, &curr))
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

bool LFSET::Remove(LFNODE& from,unsigned long x)
{
    LFNODE *pred, *curr;
    while (true)
    {
        if (false == Find(from, x, &pred, &curr))
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
            end_op();
            return true;
        }
    }
}

optional<unsigned long> LFSET::Contains(unsigned long x)
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

optional<unsigned long> LFSET::Contains(LFNODE &from, unsigned long x)
{
    start_op();
    optional<unsigned long> ret;
    LFNODE *curr = &from;
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

LFSET::~LFSET() {
    this->Init();
}
