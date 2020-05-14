#pragma once
#include <utility>
#include <atomic>
#include <optional>
#include <climits>
#include <vector>
#include <algorithm>
#include <stdexcept>

template <typename T>
struct QueueNode {
	QueueNode<T>() = default;
	QueueNode<T>(const T& val) : value{ val } {}
	QueueNode<T>(T&& val) : value{ std::move(val) } {}

	T value;
	std::atomic<QueueNode<T>*> next = nullptr;
};

template <typename T>
// Multi-Producer, Single-Consumer Queue
struct SPSCQueue {
private:
	QueueNode<T>* volatile head;
	QueueNode<T>* tail;
	struct RetiredList {
		static std::vector<RetiredNode<T>>& get() {
			static thread_local std::vector<RetiredNode<T>> retired_list;
			return retired_list;
		}
	};

	void inner_enq(QueueNode<T>& node);

public:
	SPSCQueue<T>() : head{ new QueueNode<T> }, tail{ head } {}

	std::optional<T> deq();
	void enq(const T& val);
	void enq(T&& val);
	bool is_empty() const {
		return head->next.load(std::memory_order_relaxed) == nullptr;
	}
	const T& peek() const;
	T& peek();
};

template<typename T>
inline void SPSCQueue<T>::inner_enq(QueueNode<T>& new_node)
{
	QueueNode<T>* old_tail = tail;
	tail = &new_node;
	old_tail->next.store(&new_node, std::memory_order_release);
}

template<typename T>
inline std::optional<T> SPSCQueue<T>::deq()
{
	std::optional<T> retval;
	QueueNode<T>* next_head = head->next.load(std::memory_order_acquire);

	if (next_head != nullptr) {
		auto old_head = head;
		head = next_head;
		delete old_head;
		retval = std::move(next_head->value);
	}

	return retval;
}

template<typename T>
inline void SPSCQueue<T>::enq(const T& val)
{
	this->inner_enq(*new QueueNode<T>{ val });
}

template<typename T>
inline void SPSCQueue<T>::enq(T&& val)
{
	this->inner_enq(*new QueueNode<T>{ std::move(val) });
}

template<typename T>
inline const T& SPSCQueue<T>::peek() const
{
	QueueNode<T>* old_next = head->next.load(std::memory_order_relaxed);
	if (old_next == nullptr) throw std::runtime_error("the SPSCQueue has been empty");
	return old_next->value;
}

template<typename T>
inline T& SPSCQueue<T>::peek()
{
	QueueNode<T>* old_next = head->next.load(std::memory_order_relaxed);
	if (old_next == nullptr) throw std::runtime_error("the SPSCQueue has been empty");
	return old_next->value;
}

