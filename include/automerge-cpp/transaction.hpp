#pragma once

#include <automerge-cpp/op.hpp>
#include <automerge-cpp/types.hpp>
#include <automerge-cpp/value.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace automerge_cpp {

namespace detail { struct DocState; }

class Transaction {
    friend class Document;
    explicit Transaction(detail::DocState& state);

public:
    // Map operations
    void put(const ObjId& obj, std::string_view key, ScalarValue val);
    auto put_object(const ObjId& obj, std::string_view key, ObjType type) -> ObjId;
    void delete_key(const ObjId& obj, std::string_view key);

    // List operations
    void insert(const ObjId& obj, std::size_t index, ScalarValue val);
    auto insert_object(const ObjId& obj, std::size_t index, ObjType type) -> ObjId;
    void set(const ObjId& obj, std::size_t index, ScalarValue val);
    void delete_index(const ObjId& obj, std::size_t index);

    // Text operations
    void splice_text(const ObjId& obj, std::size_t pos, std::size_t del,
                     std::string_view text);

    // Counter operations
    void increment(const ObjId& obj, std::string_view key, std::int64_t delta);

private:
    void commit();

    detail::DocState& state_;
    std::vector<Op> pending_ops_;
    std::uint64_t start_op_{0};
};

}  // namespace automerge_cpp
