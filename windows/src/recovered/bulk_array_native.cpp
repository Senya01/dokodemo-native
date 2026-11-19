#include "bulk_array_native.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
constexpr uint32_t mask28 = 0x0FFFFFFF;
int32_t signed28(uint32_t v) {
    const uint32_t p = v & mask28;
    return p & 0x08000000 ? static_cast<int32_t>(p) - 0x10000000 : static_cast<int32_t>(p);
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
BulkAddress pointer(BulkCells& cells, BulkCell value) {
    return value.is_reference ? value.reference : cells.resolve_offset(BulkNumeric(value));
}
BulkAddress array(BulkCells& cells, BulkAddress argument, const char* error) {
    const BulkCell value = cells.load(argument);
    require(value.is_reference || value.word >> 28 == 11, error);
    const BulkAddress address = pointer(cells, value);
    const BulkCell header = cells.load(address);
    require(!header.is_reference && header.word >> 28 == 9, "Bulk array header/type: original65A10 cut");
    return address;
}
}
BulkAddress BulkCells::resolve_offset(int32_t) {
    throw std::runtime_error("Bulk raw arena offset lacks an explicit mapped identity");
}
BulkAddress BulkAdvance(BulkAddress address, int64_t words) {
    const int64_t target = static_cast<int64_t>(address.index) + words;
    require(target >= 0 && target <= std::numeric_limits<uint32_t>::max(),
            "Bulk address negative/overflow protective mapping guard");
    address.index = static_cast<uint32_t>(target);
    return address;
}
int32_t BulkNumeric(BulkCell value) {
    require(!value.is_reference, "Bulk numeric symbolic address payload is outside scalar domain");
    const uint32_t tag = value.word >> 28;
    require(tag != 5 && tag != 6, "Bulk string numeric conversion C1E0C is an external cut");
    if (tag == 9) return static_cast<int32_t>(value.word & 0xFFF);
    if (tag == 4) {
        const uint32_t high_bits = (value.word >> 14) & 0x3FFF;
        const int32_t high = high_bits & 0x2000 ? static_cast<int32_t>(high_bits) - 0x4000 : static_cast<int32_t>(high_bits);
        const uint32_t low_bits = value.word & 0x3FFF;
        const int32_t low = low_bits & 0x2000 ? static_cast<int32_t>(low_bits) - 0x4000 : static_cast<int32_t>(low_bits);
        // Products and sum fit signed32 for every pair of signed14 values.
        // Proof domain assumes the original COP1/default round-to-nearest-even.
        const float sum = static_cast<float>(high * high + low * low);
        const float magnitude = std::sqrt(sum);
        return static_cast<int32_t>(magnitude);
    }
    return signed28(value.word);
}
BulkAddress BulkScalar7627C(BulkCells& cells, BulkAddress destination, int32_t argument, uint32_t selector) {
    require(selector >= 1 && selector <= 3, "Bulk scalar selector outside recovered helper set");
    const uint32_t value = static_cast<uint32_t>(BulkNumeric(cells.load(destination)));
    const uint32_t rhs = static_cast<uint32_t>(argument);
    const uint32_t output = selector == 1 ? value | rhs : selector == 2 ? value & rhs : value ^ rhs;
    BulkCell result; result.word = 0x20000000 | (output & mask28);
    cells.store(destination, result);
    return destination;
}
int32_t BulkArray7773C(BulkCells& cells, BulkAddress first_argument, BulkAddress second_argument) {
    const BulkAddress targets = array(cells, first_argument, "Bulk first argument type: original65A10 cut");
    const int32_t target_count = BulkNumeric(cells.load(targets));
    const BulkAddress operations = array(cells, second_argument, "Bulk second argument type: original65A10 cut");
    const int32_t words = BulkNumeric(cells.load(operations));
    require(words % 3 == 0, "Bulk triple length: original65A10 cut, -4 not yet returned");
    const int32_t triple_count = words / 3;
    for (int32_t triple = 0; triple < triple_count; ++triple) {
        const BulkAddress record = BulkAdvance(operations, 1 + 3LL * triple);
        const int32_t index = BulkNumeric(cells.load(record));
        const int32_t operation = BulkNumeric(cells.load(BulkAdvance(record, 1)));
        const BulkAddress argument = BulkAdvance(record, 2);
        require(operation >= 0 && operation < 17, "Bulk operation range: original65A10 cut");
        if (operation == 16) continue;
        require(operation <= 3, "Bulk operation4..15: original65A10 cut");
        for (int32_t element = 0; element < target_count; ++element) {
            const BulkAddress base = pointer(cells, cells.load(BulkAdvance(targets, 1LL + element)));
            const BulkAddress destination = BulkAdvance(base, static_cast<int64_t>(index) + 1);
            if (operation == 0) {
                const BulkCell source = cells.load(argument);
                require(source.is_reference || source.word >> 28 != 6, "Bulk owned copy75778/753E0 cut");
                cells.store(destination, source); // Original75844 does not release old destination.
            } else {
                const int32_t value = BulkNumeric(cells.load(argument));
                BulkScalar7627C(cells, destination, value, static_cast<uint32_t>(operation));
            }
        }
    }
    return 0;
}
int32_t BulkBuiltin97(BulkCells& cells, BulkAddress first_argument, BulkAddress result) {
    const auto clear = [&](BulkAddress a) {
        const BulkCell v = cells.load(a);
        require(v.is_reference || v.word >> 28 != 6, "Bulk owned release63378 cut");
        cells.store(a, {});
    };
    clear(result); //684DC prior result release before either argument read.
    const BulkAddress second_argument = BulkAdvance(first_argument, 1);
    const int32_t value = BulkArray7773C(cells, first_argument, second_argument);
    BulkCell output; output.word = 0x20000000 | (static_cast<uint32_t>(value) & mask28);
    cells.store(result, output);
    clear(first_argument); clear(second_argument);
    return value;
}
} // namespace dokodemo::recovered
