#pragma once
#include "bulk_array_native.h"
#include "string_native.h"
#include <stdexcept>

namespace dokodemo::recovered {
// A boundary is not a source return value. In particular65A10 is unclosed.
class Array98Cut : public std::runtime_error {
public:
    Array98Cut(uint32_t source_va, const char* message);
    uint32_t source_va() const noexcept { return source_va_; }
private:
    uint32_t source_va_;
};

// Original7A64C. The caller supplies CURRENT cells, including inactive stack
// slots, table identities and offset mapping. No row-length check is invented.
// Reads/stores are ordered and live; a later cut retains earlier mutations.
// Tag5/6 numeric conversion C1E0C and numeric symbolic references remain cuts.
// String equality/copy/release use the unchanged CURRENT StringArena only when
// reached. nullptr authorizes scalar/reference work, never heap readiness.
// StringArena's checked failure policy does not reproduce raw partial faults.
int32_t Array98Apply(BulkCells& cells, BulkAddress rows_argument,
    BulkAddress predicates_argument, BulkAddress actions_argument,
    StringArena* current_strings = nullptr);

// Connected684DC/6A704/689A8 lifecycle: release+clear result first, apply the
// three LIVE argument cells, store tagged result, then release+clear arg0..2.
// PC/SP/FP/command dispatch and candidate rollback belong to the caller.
// This function does not capture arguments or create a new StringArena.
int32_t Array98Builtin(BulkCells& cells, BulkAddress first_argument,
    BulkAddress result, StringArena* current_strings = nullptr);
} // namespace dokodemo::recovered
