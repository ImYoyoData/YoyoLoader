#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace yoyo::nbt {

enum class Type : uint8_t {
    End = 0,
    Byte = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Float = 5,
    Double = 6,
    ByteArray = 7,
    String = 8,
    List = 9,
    Compound = 10,
    IntArray = 11,
    LongArray = 12,
};

struct Tag;
using TagPtr = std::shared_ptr<Tag>;

struct Tag {
    Type type = Type::End;
    int64_t integer = 0;
    double floating = 0.0;
    std::string text;
    std::vector<uint8_t> byte_array;
    std::vector<int32_t> int_array;
    std::vector<int64_t> long_array;
    std::vector<TagPtr> list;
    std::vector<std::pair<std::string, TagPtr>> compound;

    TagPtr find(const std::string& key) const;
    bool erase(const std::string& key);
    void set(const std::string& key, TagPtr value);
};

TagPtr make_byte(int8_t value);
TagPtr make_short(int16_t value);
TagPtr make_int(int32_t value);
TagPtr make_long(int64_t value);
TagPtr make_float(float value);
TagPtr make_double(double value);
TagPtr make_string(std::string value);
TagPtr make_list();
TagPtr make_compound();
TagPtr make_byte_array(std::vector<uint8_t> value);
TagPtr make_int_array(std::vector<int32_t> value);
TagPtr make_long_array(std::vector<int64_t> value);

const char* type_name(Type type);

/* Bedrock little-endian NBT. `network` uses varint string lengths. */
TagPtr parse(const uint8_t* data, size_t size, bool network = false);
std::vector<uint8_t> serialize(const Tag& root, bool network = false);

std::string to_hex(const std::vector<uint8_t>& data);
std::vector<uint8_t> from_hex(const std::string& text);

} // namespace yoyo::nbt
