#include "nbt.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace yoyo::nbt {
namespace {

constexpr size_t kMaxString = 16u * 1024u * 1024u;
constexpr int32_t kMaxElements = 4 * 1024 * 1024;

struct Reader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    bool ok = true;
    bool network = false;

    bool need(size_t n) {
        if (!ok || n > size - pos) {
            ok = false;
            return false;
        }
        return true;
    }
    uint8_t u8() {
        return need(1) ? data[pos++] : 0;
    }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = static_cast<uint16_t>(data[pos]) | (static_cast<uint16_t>(data[pos + 1]) << 8);
        pos += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
                     (static_cast<uint32_t>(data[pos + 2]) << 16) | (static_cast<uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    int64_t i64() {
        if (!need(8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
        pos += 8;
        return static_cast<int64_t>(v);
    }
    float f32() {
        uint32_t bits = u32();
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    double f64() {
        uint64_t bits = static_cast<uint64_t>(i64());
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    uint32_t varint() {
        uint32_t value = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            uint8_t b = u8();
            if (!ok) return 0;
            value |= static_cast<uint32_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) return value;
        }
        ok = false;
        return 0;
    }
    std::string str() {
        uint32_t length = network ? varint() : u16();
        if (!ok || length > kMaxString || !need(length)) {
            ok = false;
            return {};
        }
        std::string out(reinterpret_cast<const char*>(data + pos), length);
        pos += length;
        return out;
    }
};

struct Writer {
    std::vector<uint8_t> out;
    bool network = false;

    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void i64(int64_t v) {
        uint64_t u = static_cast<uint64_t>(v);
        for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((u >> (8 * i)) & 0xFF));
    }
    void f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }
    void f64(double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        i64(static_cast<int64_t>(bits));
    }
    void varint(uint32_t v) {
        while (v >= 0x80) {
            out.push_back(static_cast<uint8_t>(v | 0x80));
            v >>= 7;
        }
        out.push_back(static_cast<uint8_t>(v));
    }
    void str(const std::string& s) {
        if (network) {
            varint(static_cast<uint32_t>(s.size()));
        } else {
            u16(static_cast<uint16_t>(s.size()));
        }
        out.insert(out.end(), s.begin(), s.end());
    }
};

TagPtr parse_payload(Type type, Reader& reader) {
    switch (type) {
    case Type::Byte: return make_byte(static_cast<int8_t>(reader.u8()));
    case Type::Short: return make_short(static_cast<int16_t>(reader.u16()));
    case Type::Int: return make_int(reader.i32());
    case Type::Long: return make_long(reader.i64());
    case Type::Float: return make_float(reader.f32());
    case Type::Double: return make_double(reader.f64());
    case Type::String: return make_string(reader.str());
    case Type::ByteArray: {
        int32_t count = reader.i32();
        if (!reader.ok || count < 0 || count > kMaxElements || !reader.need(static_cast<size_t>(count))) {
            return nullptr;
        }
        std::vector<uint8_t> bytes(reader.data + reader.pos, reader.data + reader.pos + count);
        reader.pos += count;
        return make_byte_array(std::move(bytes));
    }
    case Type::IntArray: {
        int32_t count = reader.i32();
        if (!reader.ok || count < 0 || count > kMaxElements) return nullptr;
        std::vector<int32_t> values;
        values.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) values.push_back(reader.i32());
        return reader.ok ? make_int_array(std::move(values)) : nullptr;
    }
    case Type::LongArray: {
        int32_t count = reader.i32();
        if (!reader.ok || count < 0 || count > kMaxElements) return nullptr;
        std::vector<int64_t> values;
        values.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) values.push_back(reader.i64());
        return reader.ok ? make_long_array(std::move(values)) : nullptr;
    }
    case Type::List: {
        auto element_type = static_cast<Type>(reader.u8());
        int32_t count = reader.i32();
        if (!reader.ok || count < 0 || count > kMaxElements) return nullptr;
        auto tag = make_list();
        tag->integer = static_cast<int64_t>(element_type);
        tag->list.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            auto element = parse_payload(element_type, reader);
            if (!element) return nullptr;
            tag->list.push_back(std::move(element));
        }
        return tag;
    }
    case Type::Compound: {
        auto tag = make_compound();
        for (;;) {
            auto child_type = static_cast<Type>(reader.u8());
            if (!reader.ok) return nullptr;
            if (child_type == Type::End) break;
            std::string name = reader.str();
            if (!reader.ok) return nullptr;
            auto child = parse_payload(child_type, reader);
            if (!child) return nullptr;
            tag->compound.emplace_back(std::move(name), std::move(child));
        }
        return tag;
    }
    default: return nullptr;
    }
}

void write_payload(const Tag& tag, Writer& writer) {
    switch (tag.type) {
    case Type::Byte: writer.u8(static_cast<uint8_t>(tag.integer)); break;
    case Type::Short: writer.u16(static_cast<uint16_t>(tag.integer)); break;
    case Type::Int: writer.i32(static_cast<int32_t>(tag.integer)); break;
    case Type::Long: writer.i64(tag.integer); break;
    case Type::Float: writer.f32(static_cast<float>(tag.floating)); break;
    case Type::Double: writer.f64(tag.floating); break;
    case Type::String: writer.str(tag.text); break;
    case Type::ByteArray:
        writer.i32(static_cast<int32_t>(tag.byte_array.size()));
        writer.out.insert(writer.out.end(), tag.byte_array.begin(), tag.byte_array.end());
        break;
    case Type::IntArray:
        writer.i32(static_cast<int32_t>(tag.int_array.size()));
        for (int32_t value : tag.int_array) writer.i32(value);
        break;
    case Type::LongArray:
        writer.i32(static_cast<int32_t>(tag.long_array.size()));
        for (int64_t value : tag.long_array) writer.i64(value);
        break;
    case Type::List: {
        Type element_type = static_cast<Type>(tag.integer);
        if (!tag.list.empty() && element_type == Type::End) {
            element_type = tag.list.front()->type;
        }
        writer.u8(static_cast<uint8_t>(element_type));
        writer.i32(static_cast<int32_t>(tag.list.size()));
        for (const auto& element : tag.list) {
            if (element) write_payload(*element, writer);
            else write_payload(Tag{}, writer);
        }
        break;
    }
    case Type::Compound:
        for (const auto& [name, child] : tag.compound) {
            if (!child) continue;
            writer.u8(static_cast<uint8_t>(child->type));
            writer.str(name);
            write_payload(*child, writer);
        }
        writer.u8(static_cast<uint8_t>(Type::End));
        break;
    default: break;
    }
}

} // namespace

TagPtr Tag::find(const std::string& key) const {
    for (const auto& [name, value] : compound) {
        if (name == key) return value;
    }
    return nullptr;
}

bool Tag::erase(const std::string& key) {
    for (auto it = compound.begin(); it != compound.end(); ++it) {
        if (it->first == key) {
            compound.erase(it);
            return true;
        }
    }
    return false;
}

void Tag::set(const std::string& key, TagPtr value) {
    for (auto& [name, existing] : compound) {
        if (name == key) {
            existing = std::move(value);
            return;
        }
    }
    compound.emplace_back(key, std::move(value));
}

TagPtr make_byte(int8_t value) {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::Byte;
    tag->integer = value;
    return tag;
}
TagPtr make_short(int16_t value) {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::Short;
    tag->integer = value;
    return tag;
}
TagPtr make_int(int32_t value) {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::Int;
    tag->integer = value;
    return tag;
}
TagPtr make_long(int64_t value) {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::Long;
    tag->integer = value;
    return tag;
}
TagPtr make_float(float value) {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::Float;
    tag->floating = value;
    return tag;
}
TagPtr make_double(double value) {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::Double;
    tag->floating = value;
    return tag;
}
TagPtr make_string(std::string value) {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::String;
    tag->text = std::move(value);
    return tag;
}
TagPtr make_list() {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::List;
    return tag;
}
TagPtr make_compound() {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::Compound;
    return tag;
}
TagPtr make_byte_array(std::vector<uint8_t> value) {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::ByteArray;
    tag->byte_array = std::move(value);
    return tag;
}
TagPtr make_int_array(std::vector<int32_t> value) {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::IntArray;
    tag->int_array = std::move(value);
    return tag;
}
TagPtr make_long_array(std::vector<int64_t> value) {
    auto tag = std::make_shared<Tag>();
    tag->type = Type::LongArray;
    tag->long_array = std::move(value);
    return tag;
}

const char* type_name(Type type) {
    switch (type) {
    case Type::End: return "end";
    case Type::Byte: return "byte";
    case Type::Short: return "short";
    case Type::Int: return "int";
    case Type::Long: return "long";
    case Type::Float: return "float";
    case Type::Double: return "double";
    case Type::ByteArray: return "byteArray";
    case Type::String: return "string";
    case Type::List: return "list";
    case Type::Compound: return "compound";
    case Type::IntArray: return "intArray";
    case Type::LongArray: return "longArray";
    default: return "unknown";
    }
}

TagPtr parse(const uint8_t* data, size_t size, bool network) {
    if (data == nullptr || size < 3) return nullptr;
    Reader reader{data, size, 0, true, network};
    auto root_type = static_cast<Type>(reader.u8());
    std::string name = reader.str();
    if (!reader.ok || root_type == Type::End) return nullptr;
    auto root = parse_payload(root_type, reader);
    if (!reader.ok) return nullptr;
    return root;
}

std::vector<uint8_t> serialize(const Tag& root, bool network) {
    Writer writer;
    writer.network = network;
    writer.u8(static_cast<uint8_t>(root.type));
    writer.str("");
    write_payload(root, writer);
    return writer.out;
}

std::string to_hex(const std::vector<uint8_t>& data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0F]);
    }
    return out;
}

std::vector<uint8_t> from_hex(const std::string& text) {
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<uint8_t> out;
    if (text.size() % 2 != 0) return out;
    out.reserve(text.size() / 2);
    for (size_t i = 0; i < text.size(); i += 2) {
        int hi = nibble(text[i]);
        int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

} // namespace yoyo::nbt
