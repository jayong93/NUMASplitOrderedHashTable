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
  ReusableAddresses<T>() { addresses.reserve(INITIAL_POOL_CAPACITY); }
  ReusableAddresses<T>(const ReusableAddresses<T> &) = delete;
  ReusableAddresses<T> &operator=(const ReusableAddresses<T> &) = delete;

  bool is_empty() const { return addresses.empty(); }
  T *pop() {
    auto ptr = addresses.back();
    addresses.pop_back();
    return ptr;
  }
  void push(T *ptr) { addresses.emplace_back(ptr); }
};

template <typename T> class MemoryChunk {
  T* chunk;
  size_t capacity;
  size_t size;

public:
  MemoryChunk<T>() : MemoryChunk<T>{0} {}
  MemoryChunk<T>(size_t capacity) : capacity{capacity}, size{0} {
      chunk = new T[capacity];
  }
  MemoryChunk<T>(const MemoryChunk<T> &) = delete;
  MemoryChunk<T> &operator=(const MemoryChunk<T> &) = delete;
  MemoryChunk<T>(MemoryChunk<T>&& other) : chunk{move(other.chunk)}, capacity{other.capacity}, size{other.size} {}

  T *push(unsigned long key, unsigned long value) {
    if (capacity <= size)
      return nullptr;
    T* elem = chunk + size;
    size++;
    new (elem) T{key, value};
    return elem;
  }
  size_t get_capacity() const { return capacity; }
};

template <typename T> class PoolAllocator {
  vector<MemoryChunk<T>> pool;
  ReusableAddresses<T> recycle_pool;

public:
  PoolAllocator() { pool.emplace_back(INITIAL_POOL_CAPACITY); }
  T *alloc(unsigned long key, unsigned long value) {
    if (recycle_pool.is_empty()) {
      MemoryChunk<T> *last_pool = &pool.back();
      auto ptr = last_pool->push(key, value);
      if (ptr == nullptr) {
        pool.emplace_back(last_pool->get_capacity() * 2);
        last_pool = &pool.back();
        auto new_t = last_pool->push(key, value);
        if (new_t->key != key) {
          cout << "when new pool has been allocated, key is : " << key << ", ";
          cout << "new_t->key is : " << new_t->key << endl;
        }
        return new_t;
      } else {
        if (ptr->key != key) {
          cout << "when a node has normally been allocated, key is : " << key
               << ", ";
          cout << "new_t->key is : " << ptr->key << endl;
        }
        return ptr;
      }
    } else {
      auto ptr = recycle_pool.pop();
      *ptr = T{key, value};
      if (ptr->key != key) {
        cout << "when a node has been recycled, key is : " << key << ", ";
        cout << "new_t->key is : " << ptr->key << endl;
      }
      return ptr;
    }
  }
  void dealloc(T *ptr) { recycle_pool.push(ptr); }
};

#endif
