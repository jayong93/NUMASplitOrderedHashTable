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

constexpr unsigned MAX_THREAD = 128;
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

class LFSET
{
    LFNODE head;

public:
    LFSET();
    void Init();
    void Dump();
    bool Find(LFNODE &from, unsigned long x, LFNODE **pred, LFNODE **curr);
    // 성공하면 삽입된 노드 pointer 반환, 실패하면 이미 삽입된 노드의 pointer 반환
    LFNODE *Add(LFNODE &from, unsigned long x, unsigned long value = 0);
    bool Add(LFNODE &from, LFNODE &node);
    bool Remove(LFNODE &from, unsigned long x);
    optional<unsigned long> Contains(unsigned long x);
    optional<unsigned long> Contains(LFNODE &from, unsigned long x);
    LFNODE& get_head() {return head;}
};
#endif /* CDC7572F_E1AD_4B7D_B182_4CA81AA68BB4 */
