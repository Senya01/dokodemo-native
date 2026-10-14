#pragma once
#include <cstdint>

namespace dokodemo::recovered {
// Symbolic identities in the explicitly mapped arena. Ctx28 is a separate,
// read-only original table, never a global-table alias or a host allocation.
struct BulkAddress {
    enum class Table : uint8_t { Stack, Global, Local, Ctx28, Result };
    Table table = Table::Stack;
    uint32_t index = 0;
};
struct BulkCell {
    uint32_t word = 0;
    bool is_reference = false;
    BulkAddress reference;
};
class BulkCells {
public:
    virtual ~BulkCells() = default;
    virtual BulkCell load(BulkAddress address) = 0;
    virtual void store(BulkAddress address, BulkCell value) = 0;
    // A raw offset needs an independently supplied arena mapping. Production
    // callers reject unresolved raw offsets rather than inventing PSP bases.
    virtual BulkAddress resolve_offset(int32_t byte_offset);
};
BulkAddress BulkAdvance(BulkAddress address, int64_t words);
// BOOT's numeric conversion: signed28 except tag9's unsigned low12, tag4's
// two signed14 components -> single-precision magnitude -> truncation.
// Numeric symbolic references and string conversions C1E0C are explicit cuts.
int32_t BulkNumeric(BulkCell cell);
// Selector1/2/3 are the original OR7627C/AND763A8/XOR764D4 helpers. They
// capture input before their integer-tag store; no destination release occurs.
BulkAddress BulkScalar7627C(BulkCells& cells, BulkAddress destination, int32_t argument, uint32_t selector);
// Original pointer/type/triple checks, source element and argument rereads,
// copy7581C, selectors1..3/16, return0. Error reporter65A10 is a cut, not a
// fabricated -1/-2/-3/-4 return. Partial stores survive a helper exception;
// StartupProbe runs this on a candidate and atomically rejects unsupported paths.
int32_t BulkArray7773C(BulkCells& cells, BulkAddress first_argument, BulkAddress second_argument);
// Joined684DC/6A6A8/689A8 value lifecycle. Result is a distinct mapped
// subobject in StartupProbe; tests may additionally inspect captured aliases.
// Caller commits consumed SP only after this operation succeeds.
int32_t BulkBuiltin97(BulkCells& cells, BulkAddress first_argument, BulkAddress result);
} // namespace dokodemo::recovered
