#include "experiment/ExperimentCsvCodec.h"

#include <charconv>
#include <cmath>
#include <istream>
#include <iterator>
#include <limits>
#include <ostream>
#include <set>
#include <stdexcept>

namespace ParallelRoam::Experiment
{
namespace
{
void ValidateUtf8(std::string_view text)
{
    for (std::size_t index = 0; index < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[index++]);
        if (first < 0x80U)
        {
            if (first == 0U)
            {
                throw std::runtime_error("CSV contains a NUL byte");
            }
            continue;
        }
        // 拒绝过长编码、代理区和超出 Unicode 范围的字节序列
        const unsigned count = first >= 0xC2U && first <= 0xDFU ? 1U :
            first >= 0xE0U && first <= 0xEFU ? 2U : first >= 0xF0U && first <= 0xF4U ? 3U : 0U;
        if (count == 0U || index + count > text.size())
        {
            throw std::runtime_error("Invalid UTF-8 in CSV");
        }
        std::uint32_t code = first & (0x7FU >> count);
        for (unsigned offset = 0; offset < count; ++offset)
        {
            const auto next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xC0U) != 0x80U)
            {
                throw std::runtime_error("Invalid UTF-8 continuation in CSV");
            }
            code = (code << 6U) | (next & 0x3FU);
        }
        if ((count == 1U && code < 0x80U) || (count == 2U && code < 0x800U) ||
            (count == 3U && code < 0x10000U) || code > 0x10FFFFU ||
            (code >= 0xD800U && code <= 0xDFFFU))
        {
            throw std::runtime_error("Invalid Unicode scalar in CSV");
        }
    }
}

template <typename Value>
std::string FormatReal(Value value)
{
    if (!std::isfinite(value))
    {
        throw std::runtime_error("Non-finite CSV number");
    }
    char buffer[64];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value,
        std::chars_format::general, std::numeric_limits<Value>::max_digits10);
    if (error != std::errc{})
    {
        throw std::runtime_error("CSV number formatting failed");
    }
    return {buffer, end};
}
} // 匿名命名空间

ExperimentCsvTable ReadExperimentCsv(std::istream& input, std::string_view source)
{
    try
    {
        std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        if (input.bad())
        {
            throw std::runtime_error("CSV read failed");
        }
        ValidateUtf8(bytes);
        if (bytes.starts_with("\xEF\xBB\xBF"))
        {
            bytes.erase(0, 3);
        }
        std::vector<ExperimentCsvRow> rows;
        ExperimentCsvRow row;
        std::string field;
        // quoted 与 closed 分别表示引号内及闭合后等待分隔，二者互斥
        // started 标记字段已有语法输入，空引号字段也算开始，不能仅凭 field 是否为空判断
        bool quoted = false;
        bool closed = false;
        bool started = false;
        std::size_t line = 1;
        const auto fail = [&line](const char* message) {
            throw std::runtime_error("line " + std::to_string(line) + ": " + message);
        };
        const auto finishField = [&] {
            row.push_back(std::move(field));
            field.clear();
            closed = false;
            started = false;
        };
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            const char value = bytes[index];
            if (quoted)
            {
                if (value == '"')
                {
                    // 成对双引号属于字段内容，只有未配对的双引号才能结束引号字段
                    if (index + 1 < bytes.size() && bytes[index + 1] == '"')
                    {
                        field += '"';
                        ++index;
                    }
                    else
                    {
                        quoted = false;
                        closed = true;
                    }
                }
                else
                {
                    field += value;
                    line += value == '\n' ? 1U : 0U;
                }
                continue;
            }
            if (value == ',')
            {
                finishField();
            }
            else if (value == '\r' || value == '\n')
            {
                // 记录分隔接受 LF 或 CRLF，字段内换行由引号状态处理
                if (value == '\r')
                {
                    if (index + 1 >= bytes.size() || bytes[++index] != '\n')
                    {
                        fail("bare CR outside quoted field");
                    }
                }
                finishField();
                rows.push_back(std::move(row));
                row.clear();
                ++line;
            }
            else if (value == '"' && !started && !closed)
            {
                quoted = true;
                started = true;
            }
            else
            {
                // 闭合引号后只允许分隔符或 EOF，禁止拼接裸文本或在字段中途开启引号
                if (closed || value == '"')
                {
                    fail("unexpected text or quote");
                }
                field += value;
                started = true;
            }
        }
        if (quoted)
        {
            fail("unterminated quoted field");
        }
        // 末尾逗号使 row 非空，因此必须补上尾部空字段
        // 换行已清空 row 和字段状态，EOF 不再追加一条空记录
        if (started || closed || !row.empty())
        {
            finishField();
            rows.push_back(std::move(row));
        }
        if (rows.empty())
        {
            throw std::runtime_error("CSV header is missing");
        }
        ExperimentCsvTable table;
        table.Header = std::move(rows.front());
        std::set<std::string> names;
        for (const auto& name : table.Header)
        {
            if (name.empty() || !names.insert(name).second)
            {
                throw std::runtime_error("Empty or duplicate CSV column");
            }
        }
        for (std::size_t index = 1; index < rows.size(); ++index)
        {
            if (rows[index].size() != table.Header.size())
            {
                throw std::runtime_error("record " + std::to_string(index + 1) + ": CSV column count mismatch");
            }
            table.Rows.push_back(std::move(rows[index]));
        }
        return table;
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error(std::string{source} + ": " + error.what());
    }
}

void WriteExperimentCsvRow(std::ostream& output, const ExperimentCsvRow& row)
{
    for (std::size_t index = 0; index < row.size(); ++index)
    {
        if (index != 0)
        {
            output << ',';
        }
        const auto& field = row[index];
        ValidateUtf8(field);
        const bool quote = field.find_first_of(",\"\r\n") != std::string::npos;
        if (quote) output << '"';
        for (const char value : field)
        {
            if (quote && value == '"') output << '"';
            output << value;
        }
        if (quote) output << '"';
    }
    output << '\n';
    if (!output)
    {
        throw std::runtime_error("CSV write failed");
    }
}

void RequireExperimentCsvHeader(const ExperimentCsvTable& table, const ExperimentCsvRow& header)
{
    if (table.Header != header)
    {
        throw std::runtime_error("CSV schema columns or order mismatch");
    }
}

std::uint64_t ParseExperimentCsvUnsigned(std::string_view value)
{
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size())
    {
        throw std::runtime_error("Invalid unsigned CSV integer: " + std::string{value});
    }
    return result;
}

float ParseExperimentCsvFloat(std::string_view value)
{
    float result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size() || !std::isfinite(result))
    {
        throw std::runtime_error("Invalid finite CSV float: " + std::string{value});
    }
    return result;
}

std::string FormatExperimentCsvFloat(float value) { return FormatReal(value); }
std::string FormatExperimentCsvDouble(double value) { return FormatReal(value); }
} // 命名空间 ParallelRoam::Experiment
