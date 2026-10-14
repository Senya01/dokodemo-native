#include "array98_native.h"

namespace dokodemo::recovered {
Array98Cut::Array98Cut(uint32_t va, const char* message)
    : std::runtime_error(message), source_va_(va) {}
namespace {
void require(bool condition, uint32_t va, const char* message) {
    if (!condition) throw Array98Cut(va, message);
}
int32_t numeric(BulkCell value) {
    require(!value.is_reference, 0x75078, "Array98 numeric symbolic offset lacks an inverse mapping");
    require(value.word >> 28 != 5 && value.word >> 28 != 6, 0xC1E0C,
        "Array98 string numeric conversion remains unclosed");
    return BulkNumeric(value);
}
StringArena& strings(StringArena* arena) {
    require(arena != nullptr && arena->ready(), 0x75778,
        "Array98 reached string operation without CURRENT mapped StringArena");
    return *arena;
}
BulkAddress pointer(BulkCells& cells, BulkCell value) {
    return value.is_reference ? value.reference : cells.resolve_offset(numeric(value));
}
BulkAddress array(BulkCells& cells, BulkAddress argument) {
    const BulkCell value = cells.load(argument);
    require(value.is_reference || value.word >> 28 == 11, 0x65A10,
        "Array98 argument/row reference diagnostic is an unclosed source call");
    const BulkAddress address = pointer(cells, value);
    const BulkCell header = cells.load(address);
    require(!header.is_reference && header.word >> 28 == 9, 0x65A10,
        "Array98 array header diagnostic is an unclosed source call");
    return address;
}
bool equal76858(BulkCells& cells, BulkAddress left, BulkAddress right, StringArena* arena) {
    const BulkCell a = cells.load(left);
    const uint32_t tag = a.is_reference ? 11 : a.word >> 28;
    if (tag >= 11 || tag == 9) return false; // Original76880 initializes a1=0.
    const BulkCell b = cells.load(right);
    const uint32_t other = b.is_reference ? 11 : b.word >> 28;
    if (tag == 0) return other == 0;
    if (tag == 1) return other == 1 && (a.word & 1) == (b.word & 1);
    if (tag == 5 || tag == 6) {
        if (other != 5 && other != 6) return false;
        StringArena& s = strings(arena);
        // Both getter calls occur before strcmp and advance the live cursor.
        uint32_t p = s.resolve75778(a.word);
        uint32_t q = s.resolve75778(cells.load(right).word);
        for (;;) {
            const uint8_t x = s.bytes(p++, 1)[0];
            const uint8_t y = s.bytes(q++, 1)[0];
            if (x != y) return false;
            if (x == 0) return true;
        }
    }
    return !b.is_reference && a.word == b.word;
}
bool predicate(BulkCells& cells, BulkAddress target, int32_t op,
               BulkAddress argument, StringArena* arena) {
    if (op == 16) return true;
    if (op == 0) return equal76858(cells, target, argument, arena);
    if (op == 1) return !equal76858(cells, target, argument, arena);
    require(op >= 2 && op <= 8, 0x65A10, "Array98 predicate dispatch diagnostic remains unclosed");
    const int32_t a = numeric(cells.load(target));
    const int32_t b = numeric(cells.load(argument));
    switch (op) {
    case 2: return a > b;
    case 3: return a >= b;
    case 4: return a < b;
    case 5: return a <= b;
    case 6:
        // Original7B2CC reuses/reconverts the argument after capturing a&b.
        return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) ==
            static_cast<uint32_t>(numeric(cells.load(argument)));
    case 7: return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) == 0;
    case 8: return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
    default: throw Array98Cut(0x65A10, "Array98 predicate dispatch diagnostic remains unclosed");
    }
}
void copy7581C(BulkCells& cells, BulkAddress target, BulkAddress argument, StringArena* arena) {
    const BulkCell source = cells.load(argument);
    if (source.is_reference || source.word >> 28 != 6) {
        cells.store(target, source); // Source75844: no release, even over tag6.
        return;
    }
    uint32_t value = cells.load(target).word;
    strings(arena).copy7581C(value, source.word);
    BulkCell output; output.word = value;
    cells.store(target, output);
}
void action(BulkCells& cells, BulkAddress target, int32_t op,
            BulkAddress argument, StringArena* arena) {
    if (op == 16) return;
    if (op == 0) { copy7581C(cells, target, argument, arena); return; }
    require(op >= 1 && op <= 3, 0x65A10, "Array98 action dispatch diagnostic remains unclosed");
    // Original7AB4C: capture argument before the destination helper reads it.
    const int32_t value = numeric(cells.load(argument));
    (void)BulkScalar7627C(cells, target, value, static_cast<uint32_t>(op));
}
void clear7515C(BulkCells& cells, BulkAddress address, StringArena* arena) {
    const BulkCell value = cells.load(address);
    if (!value.is_reference && value.word >> 28 == 6) {
        uint32_t word = value.word;
        strings(arena).release7515C(word);
    }
    cells.store(address, {});
}
}
int32_t Array98Apply(BulkCells& cells, BulkAddress rows_argument,
    BulkAddress predicates_argument, BulkAddress actions_argument, StringArena* arena) {
    const BulkAddress rows = array(cells, rows_argument);
    const int32_t row_count = numeric(cells.load(rows));
    const BulkAddress conditions = array(cells, predicates_argument);
    const int32_t condition_words = numeric(cells.load(conditions));
    require(condition_words % 3 == 0, 0x65A10, "Array98 predicate triple diagnostic remains unclosed");
    const int32_t condition_count = condition_words / 3;
    const BulkAddress actions = array(cells, actions_argument);
    const int32_t action_words = numeric(cells.load(actions));
    require(action_words % 3 == 0, 0x65A10, "Array98 action triple diagnostic remains unclosed");
    const int32_t action_count = action_words / 3;
    int32_t selected = 0;
    for (int32_t row = 0; row < row_count; ++row) {
        const BulkAddress object = array(cells, BulkAdvance(rows, 1LL + row));
        bool matches = false;
        for (int32_t i = 0; i < condition_count; ++i) {
            const BulkAddress triple = BulkAdvance(conditions, 1 + 3LL * i);
            const int32_t index = numeric(cells.load(triple));
            const int32_t op = numeric(cells.load(BulkAdvance(triple, 1)));
            require(op >= 0 && op <= 16, 0x65A10, "Array98 predicate range diagnostic remains unclosed");
            // Compute even for16; only a reached operation accesses the cell.
            const BulkAddress target = BulkAdvance(object, 1LL + index);
            matches = predicate(cells, target, op, BulkAdvance(triple, 2), arena);
            if (!matches) break;
        }
        if (!matches) continue;
        for (int32_t i = 0; i < action_count; ++i) {
            const BulkAddress triple = BulkAdvance(actions, 1 + 3LL * i);
            const int32_t index = numeric(cells.load(triple));
            const int32_t op = numeric(cells.load(BulkAdvance(triple, 1)));
            require(op >= 0 && op <= 16, 0x65A10, "Array98 action range diagnostic remains unclosed");
            action(cells, BulkAdvance(object, 1LL + index), op, BulkAdvance(triple, 2), arena);
        }
        ++selected;
    }
    return selected;
}
int32_t Array98Builtin(BulkCells& cells, BulkAddress first_argument,
    BulkAddress result, StringArena* arena) {
    clear7515C(cells, result, arena);
    const int32_t value = Array98Apply(cells, first_argument,
        BulkAdvance(first_argument, 1), BulkAdvance(first_argument, 2), arena);
    BulkCell output; output.word = 0x20000000 | (static_cast<uint32_t>(value) & 0x0FFFFFFF);
    cells.store(result, output);
    for (int32_t i = 0; i < 3; ++i) clear7515C(cells, BulkAdvance(first_argument, i), arena);
    return value;
}
} // namespace dokodemo::recovered
