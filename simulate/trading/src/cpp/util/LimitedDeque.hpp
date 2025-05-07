/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <deque>

//-------------------------------------------------------------------------

template<typename T>
class LimitedDeque : public std::deque<T>
{
public:
    using Base = std::deque<T>;

    explicit LimitedDeque(size_t capacity) noexcept : m_capacity{capacity} {}

    [[nodiscard]] size_t capacity() const noexcept { return m_capacity; }

    void push_back(T item)
    {
        Base::push_back(item);
        if (Base::size() > m_capacity) {
            Base::pop_front();
        }
    }

    void push_front(T item)
    {
        Base::push_front(item);
        if (Base::size() > m_capacity) {
            Base::pop_back();
        }
    }

private:
    size_t m_capacity;
};

//-------------------------------------------------------------------------
