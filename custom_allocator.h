#ifndef __CUSTOM_ALLOCATOR_H__
#define __CUSTOM_ALLOCATOR_H__

#include <iostream>
#include <utility>
#include <vector>

using namespace std;

constexpr size_t INITIAL_POOL_CAPACITY{512};

template <typename T> class ReusableAddresses {
  vector<T *> addresses;

public:
  ReusableAddresses() {addresses.reserve(INITIAL_POOL_CAPACITY);}
  bool is_empty() const { return addresses.empty(); }
  T *pop() {
    auto ptr = addresses.back();
    addresses.pop_back();
    return ptr;
  }
  void push(T *ptr) { addresses.emplace_back(ptr); }
};

template <typename T> struct MemoryChunk {
  vector<T> chunk;
  size_t capacity;

  MemoryChunk(size_t capacity) : capacity{capacity} { chunk.reserve(capacity); }
  T* push(unsigned long key, unsigned long value) {
    if (capacity <= chunk.size()) return nullptr;
    chunk.emplace_back(key, value);
    return &chunk.back();
  }
};


template <typename T> class PoolAllocator {
  vector<MemoryChunk<T>> pool;
  ReusableAddresses<T> recycle_pool;

public:
  PoolAllocator() { pool.emplace_back(INITIAL_POOL_CAPACITY); }
  T *alloc(unsigned long key, unsigned long value) {
    if (recycle_pool.is_empty()) {
      auto &last_pool = pool.back();
      auto ptr = last_pool.push(key, value);
      if (ptr == nullptr) {
        cout << "Add new pool" << endl;
        pool.emplace_back(last_pool.capacity * 2);
        last_pool = pool.back();
        return last_pool.push(key, value);
      }
      else {
        return ptr;
      }
    } else {
      cout << "Recycle ptr" << endl;
      auto ptr = recycle_pool.pop();
      *ptr = T{key, value};
      return ptr;
    }
  }
  void dealloc(T *ptr) { recycle_pool.push(ptr); }
};

#endif
