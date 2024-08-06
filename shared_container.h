#pragma once
#include "shared_allocator.h"
#include <string>
#include <vector>
#include <deque>
#include <list>
#include <forward_list>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>

using shared_string = std::basic_string<char, std::char_traits<char>, shared_allocator<char>>;

template<class T>
using shared_vector = std::vector<T, shared_allocator<T>>;

template<class T>
using shared_deque = std::deque<T, shared_allocator<T>>;

template<class T>
using shared_list = std::list<T, shared_allocator<T>>;

template<class T>
using shared_forward_list = std::forward_list<T, shared_allocator<T>>;

template<class K, class C = std::less<K>>
using shared_set = std::set<K, C, shared_allocator<K>>;

template<class K, class C = std::less<K>>
using shared_multiset = std::multiset<K, C, shared_allocator<K>>;

template<class K, class V, class C = std::less<K>>
using shared_map = std::map<K, V, C, shared_allocator<std::pair<const K, V>>>;

template<class K, class V, class C = std::less<K>>
using shared_multimap = std::multimap<K, V, C, shared_allocator<std::pair<const K, V>>>;

template<class K, class H = std::hash<K>, class E = std::equal_to<K>>
using shared_unordered_set = std::unordered_set<K, H, E, shared_allocator<const K>>;

template<class K, class H = std::hash<K>, class E = std::equal_to<K>>
using shared_unordered_multiset = std::unordered_multiset<K, H, E, shared_allocator<const K>>;

template<class K, class V, class H = std::hash<K>, class E = std::equal_to<K>>
using shared_unordered_map = std::unordered_map<K, V, H, E, shared_allocator<std::pair<const K, V>>>;

template<class K, class V, class H = std::hash<K>, class E = std::equal_to<K>>
using shared_unordered_multimap = std::unordered_multimap<K, V, H, E, shared_allocator<std::pair<const K, V>>>;

template<class T, class C = shared_deque<T>>
using shared_queue = std::queue<T, C>;

template<class T, class C = shared_deque<T>>
using shared_stack = std::stack<T, C>;

template<class T, class S = shared_vector<T>, class C = std::less<T>>
using shared_priority_queue = std::priority_queue<T, S, C>;
