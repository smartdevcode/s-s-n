/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "taosim/book/Book.hpp"
#include "JsonSerializable.hpp"
#include "PriceTimeBook.hpp"
#include "common.hpp"

#include <pugixml.hpp>

//------------------------------------------------------------------------

class BookFactory
{
public:
    template<typename... Args>
    [[nodiscard]] static Book::Ptr createBook(std::string_view algorithm, Args&&... args)
    {
        Book::Ptr book{};

        if (algorithm == "PriceTime") {
            book = std::make_shared<PriceTimeBook>(std::forward<Args>(args)...);
        }
        else {
            throw std::invalid_argument(fmt::format(
                "{}: Unknown algorithm '{}'",
                std::source_location::current().function_name(),
                algorithm));
        }

        return book;
    }

private:
    BookFactory() noexcept = default;
};

//------------------------------------------------------------------------
