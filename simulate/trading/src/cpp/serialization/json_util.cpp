/*
 * SPDX-FileCopyrightText: 2025 Rayleigh Research <to@rayleigh.re>
 * SPDX-License-Identifier: MIT
 */
#include "json_util.hpp"

#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <source_location>

//-------------------------------------------------------------------------

namespace taosim::json
{

//-------------------------------------------------------------------------

std::string json2str(const rapidjson::Value& json, const FormatOptions& formatOptions)
{
    const auto& [indent, decimals] = formatOptions;
    rapidjson::StringBuffer buffer;
    if (indent.has_value()) {
        const auto& opts = indent.value();
        rapidjson::PrettyWriter writer{buffer};
        writer.SetIndent(opts.indentChar, opts.indentCharCount);
        writer.SetMaxDecimalPlaces(decimals);
        json.Accept(writer);
    } else {
        rapidjson::Writer writer{buffer};
        writer.SetMaxDecimalPlaces(decimals);
        json.Accept(writer);
    }
    return buffer.GetString();
}

//-------------------------------------------------------------------------

rapidjson::Document str2json(const std::string& str)
{
    rapidjson::Document json;
    if (json.Parse(str.c_str()).HasParseError()) {
        static constexpr size_t maxCharsShown = 200uz;
        std::string_view facade{str.data(), std::min(maxCharsShown, str.size())};
        throw std::invalid_argument{fmt::format(
            "{}: Error parsing Json string: {}{}",
            std::source_location::current().function_name(),
            facade,
            facade.size() < str.size() ? "..." : "")};
    }
    return json;
}

//-------------------------------------------------------------------------

void dumpJson(
    const rapidjson::Value& json,
    std::ofstream& ofs,
    const FormatOptions& formatOptions)
{
    const auto& [indent, decimals] = formatOptions;
    rapidjson::OStreamWrapper osw{ofs};
    if (indent.has_value()) {
        const auto& opts = indent.value();
        rapidjson::PrettyWriter writer{osw};
        writer.SetIndent(opts.indentChar, opts.indentCharCount);
        writer.SetMaxDecimalPlaces(decimals);
        json.Accept(writer);
        return;
    }
    rapidjson::Writer writer{osw};
    writer.SetMaxDecimalPlaces(decimals);
    json.Accept(writer);
}

//-------------------------------------------------------------------------

rapidjson::Document loadJson(const std::filesystem::path& path)
{
    static constexpr auto ctx = std::source_location::current().function_name();
    if (!std::filesystem::exists(path)) {
        throw std::invalid_argument{fmt::format("{}: No such file '{}'", ctx, path.c_str())};
    }
    std::ifstream ifs{path};
    rapidjson::IStreamWrapper isw{ifs};
    rapidjson::Document json;
    if (json.ParseStream(isw).HasParseError()) {
        throw std::invalid_argument{fmt::format(
            "{}: Unable to parse Json data from '{}'", ctx, path.c_str())};
    }
    return json;
}

//-------------------------------------------------------------------------

decimal_t getDecimal(const rapidjson::Value& json)
{
    if (json.IsUint64()) [[likely]] {
        return util::unpackDecimal(json.GetUint64());
    } else if (json.IsDouble()) [[unlikely]] {
        return decimal_t{json.GetDouble()};
    } else {
        throw std::invalid_argument{fmt::format(
            "{}: Ill-formed Json value to form a decimal with: {}",
            std::source_location::current().function_name(),
            json2str(json))};
    }
}

//-------------------------------------------------------------------------

void serializeHelper(
    rapidjson::Document& json,
    const std::string& key,
    std::function<void(rapidjson::Document&)> serializer)
{
    if (key.empty()) return serializer(json);
    auto& allocator = json.GetAllocator();
    rapidjson::Document subJson{&allocator};
    serializer(subJson);
    json.AddMember(rapidjson::Value{key.c_str(), allocator}, subJson, allocator);
}

//-------------------------------------------------------------------------

}  // namespace taosim::json

//-------------------------------------------------------------------------