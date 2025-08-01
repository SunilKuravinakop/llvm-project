//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03, c++11, c++14, c++17, c++20

// <flat_map>

// template <class InputIterator>
//   flat_map(InputIterator first, InputIterator last, const key_compare& comp = key_compare());
// template<class InputIterator, class Allocator>
//   flat_map(InputIterator first, InputIterator last, const Allocator& a);
// template<class InputIterator, class Allocator>
//   flat_map(InputIterator first, InputIterator last, const key_compare& comp, const Allocator& a);

#include <algorithm>
#include <deque>
#include <flat_map>
#include <functional>
#include <vector>
#include <ranges>

#include "MinSequenceContainer.h"
#include "min_allocator.h"
#include "test_allocator.h"
#include "test_iterators.h"
#include "test_macros.h"
#include "../helpers.h"
#include "../../../test_compare.h"

template <class KeyContainer, class ValueContainer>
constexpr void test() {
  using Key   = typename KeyContainer::value_type;
  using Value = typename ValueContainer::value_type;
  using P     = std::pair<Key, Value>;
  P ar[]      = {{1, 1}, {1, 2}, {1, 3}, {2, 4}, {2, 5}, {3, 6}, {2, 7}, {3, 8}, {3, 9}};
  {
    // flat_map(InputIterator , InputIterator)
    // cpp17_input_iterator
    using M = std::flat_map<Key, Value, std::less<Key>, KeyContainer, ValueContainer>;
    auto m  = M(cpp17_input_iterator<const P*>(ar), cpp17_input_iterator<const P*>(ar + 9));
    assert(std::ranges::equal(m.keys(), KeyContainer{1, 2, 3}));
    check_possible_values(
        m.values(),
        std::vector<std::vector<Value>>{
            {1, 2, 3},
            {4, 5, 7},
            {6, 8, 9},
        });

    // explicit(false)
    M m2 = {cpp17_input_iterator<const P*>(ar), cpp17_input_iterator<const P*>(ar + 9)};
    assert(m2 == m);
  }
  {
    // flat_map(InputIterator , InputIterator)
    // greater
    using M = std::flat_map<Key, Value, std::greater<Key>, KeyContainer, ValueContainer>;
    auto m  = M(cpp17_input_iterator<const P*>(ar), cpp17_input_iterator<const P*>(ar + 9));
    assert(std::ranges::equal(m.keys(), KeyContainer{3, 2, 1}));
    check_possible_values(
        m.values(),
        std::vector<std::vector<Value>>{
            {6, 8, 9},
            {4, 5, 7},
            {1, 2, 3},
        });
  }
  {
    // flat_map(InputIterator , InputIterator)
    // Test when the operands are of array type (also contiguous iterator type)
    using M = std::flat_map<Key, Value, std::greater<Key>, KeyContainer, ValueContainer>;
    auto m  = M(ar, ar);
    assert(m.empty());
  }
  {
    // flat_map(InputIterator , InputIterator, const key_compare&)
    using C = test_less<Key>;
    using M = std::flat_map<Key, Value, C, KeyContainer, ValueContainer>;
    auto m  = M(ar, ar + 9, C(3));
    assert(std::ranges::equal(m.keys(), KeyContainer{1, 2, 3}));
    check_possible_values(
        m.values(),
        std::vector<std::vector<Value>>{
            {1, 2, 3},
            {4, 5, 7},
            {6, 8, 9},
        });
    assert(m.key_comp() == C(3));

    // explicit(false)
    M m2 = {ar, ar + 9, C(3)};
    assert(m2 == m);
    assert(m2.key_comp() == C(3));
  }
}

template <template <class...> class KeyContainer, template <class...> class ValueContainer>
constexpr void test_alloc() {
  using P = std::pair<int, short>;
  P ar[]  = {{1, 1}, {1, 2}, {1, 3}, {2, 4}, {2, 5}, {3, 6}, {2, 7}, {3, 8}, {3, 9}};

  {
    // flat_map(InputIterator , InputIterator, const Allocator&)
    using A1 = test_allocator<int>;
    using A2 = test_allocator<short>;
    using M  = std::flat_map<int, short, std::less<int>, KeyContainer<int, A1>, ValueContainer<short, A2>>;
    auto m   = M(ar, ar + 9, A1(5));
    assert(std::ranges::equal(m.keys(), KeyContainer<int, A1>{1, 2, 3}));
    check_possible_values(
        m.values(),
        std::vector<std::vector<short>>{
            {1, 2, 3},
            {4, 5, 7},
            {6, 8, 9},
        });
    assert(m.keys().get_allocator() == A1(5));
    assert(m.values().get_allocator() == A2(5));
  }
  {
    // flat_map(InputIterator , InputIterator, const Allocator&)
    // explicit(false)
    using A1 = test_allocator<int>;
    using A2 = test_allocator<short>;
    using M  = std::flat_map<int, short, std::less<int>, KeyContainer<int, A1>, ValueContainer<short, A2>>;
    M m      = {ar, ar + 9, A1(5)}; // implicit ctor
    assert(std::ranges::equal(m.keys(), KeyContainer<int, A1>{1, 2, 3}));
    check_possible_values(
        m.values(),
        std::vector<std::vector<short>>{
            {1, 2, 3},
            {4, 5, 7},
            {6, 8, 9},
        });
    assert(m.keys().get_allocator() == A1(5));
    assert(m.values().get_allocator() == A2(5));
  }
  {
    // flat_map(InputIterator , InputIterator, const key_compare&, const Allocator&)
    using C  = test_less<int>;
    using A1 = test_allocator<int>;
    using A2 = test_allocator<short>;
    using M  = std::flat_map<int, short, C, KeyContainer<int, A1>, ValueContainer<short, A2>>;
    auto m   = M(ar, ar + 9, C(3), A1(5));
    assert(std::ranges::equal(m.keys(), KeyContainer<int, A1>{1, 2, 3}));
    check_possible_values(
        m.values(),
        std::vector<std::vector<short>>{
            {1, 2, 3},
            {4, 5, 7},
            {6, 8, 9},
        });
    assert(m.key_comp() == C(3));
    assert(m.keys().get_allocator() == A1(5));
    assert(m.values().get_allocator() == A2(5));
  }
  {
    // flat_map(InputIterator , InputIterator, const key_compare&, const Allocator&)
    // explicit(false)
    using A1 = test_allocator<int>;
    using A2 = test_allocator<short>;
    using M  = std::flat_map<int, short, std::less<int>, KeyContainer<int, A1>, ValueContainer<short, A2>>;
    M m      = {ar, ar + 9, {}, A2(5)}; // implicit ctor
    assert(std::ranges::equal(m.keys(), KeyContainer<int, A1>{1, 2, 3}));
    check_possible_values(
        m.values(),
        std::vector<std::vector<short>>{
            {1, 2, 3},
            {4, 5, 7},
            {6, 8, 9},
        });
    assert(m.keys().get_allocator() == A1(5));
    assert(m.values().get_allocator() == A2(5));
  }
}

constexpr bool test() {
  {
    // The constructors in this subclause shall not participate in overload
    // resolution unless uses_allocator_v<key_container_type, Alloc> is true
    // and uses_allocator_v<mapped_container_type, Alloc> is true.

    using C     = test_less<int>;
    using A1    = test_allocator<int>;
    using A2    = other_allocator<int>;
    using V1    = std::vector<int, A1>;
    using V2    = std::vector<int, A2>;
    using M1    = std::flat_map<int, int, C, V1, V1>;
    using M2    = std::flat_map<int, int, C, V1, V2>;
    using M3    = std::flat_map<int, int, C, V2, V1>;
    using Iter1 = typename M1::iterator;
    using Iter2 = typename M2::iterator;
    using Iter3 = typename M3::iterator;
    static_assert(std::is_constructible_v<M1, Iter1, Iter1, const A1&>);
    static_assert(!std::is_constructible_v<M1, Iter1, Iter1, const A2&>);
    static_assert(!std::is_constructible_v<M2, Iter2, Iter2, const A2&>);
    static_assert(!std::is_constructible_v<M3, Iter3, Iter3, const A2&>);

    static_assert(std::is_constructible_v<M1, Iter1, Iter1, const C&, const A1&>);
    static_assert(!std::is_constructible_v<M1, Iter1, Iter1, const C&, const A2&>);
    static_assert(!std::is_constructible_v<M2, Iter2, Iter2, const C&, const A2&>);
    static_assert(!std::is_constructible_v<M3, Iter3, Iter3, const C&, const A2&>);
  }

  test<std::vector<int>, std::vector<int>>();
  test<std::vector<int>, std::vector<double>>();
  test<MinSequenceContainer<int>, MinSequenceContainer<double>>();
  test<std::vector<int, min_allocator<int>>, std::vector<double, min_allocator<double>>>();
  test<std::vector<int, min_allocator<int>>, std::vector<int, min_allocator<int>>>();

  test_alloc<std::vector, std::vector>();

#ifndef __cpp_lib_constexpr_deque
  if (!TEST_IS_CONSTANT_EVALUATED)
#endif
  {
    test<std::deque<int>, std::vector<double>>();
    test_alloc<std::deque, std::deque>();
  }

  return true;
}

int main(int, char**) {
  test();
#if TEST_STD_VER >= 26
  static_assert(test());
#endif

  return 0;
}
