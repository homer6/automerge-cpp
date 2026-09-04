#pragma once

#include <automerge-cpp/change.hpp>

#include "storage/change_chunk.hpp"

#include <vector>

namespace automerge_cpp::detail {

struct SyncMessageEncoding {
    std::vector<Change> changes;
    std::vector<storage::EncodedChange> encoded_changes;
};

} // namespace automerge_cpp::detail
