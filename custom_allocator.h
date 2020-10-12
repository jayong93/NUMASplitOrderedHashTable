#ifndef __CUSTOM_ALLOCATOR_H__
#define __CUSTOM_ALLOCATOR_H__

#include <utility>
#include <vector>

using std::vector;

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
};


template <typename T> class PoolAllocator {
  vector<MemoryChunk<T>> pool;
  ReusableAddresses<T> recycle_pool;

public:
  PoolAllocator() { pool.emplace_back(INITIAL_POOL_CAPACITY); }
  template <typename... Param> T *alloc(Param &&... args) {
    if (recycle_pool.is_empty()) {
      auto &last_pool = pool.back();
      if (last_pool.chunk.size() == last_pool.capacity) {
        pool.emplace_back(last_pool.capacity * 2);
        last_pool = pool.back();
      }
      last_pool.chunk.emplace_back(std::forward<Param>(args)...);
      return &last_pool.chunk.back();
    } else {
      auto ptr = recycle_pool.pop();
      *ptr = T{std::forward<Param>(args)...};
      return ptr;
    }
  }
  void dealloc(T *ptr) { recycle_pool.push(ptr); }
};

#endif
