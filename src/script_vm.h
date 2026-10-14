#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Restricted KSC v20030204 reader/interpreter. No PSP CPU, OS or GPU emulation.
// Source tables and strings are data; reading them never emits dialogue.
namespace dokodemo::script {

struct Diagnostic {
    std::string reason;
    uint32_t pc = 0;             // relative to code base
    uint32_t file_offset = 0;    // pc + 28 for bytecode
    int opcode = -1;
    std::string text() const;
};
struct Source {
    std::filesystem::path path;
    std::string sha256;
};
struct Header {
    uint32_t entrypoint = 0, code_size = 0, pool_size = 0;
    uint16_t field14 = 0, local_count = 0, global_count = 0, ctx28_count = 0;
    uint32_t pool_start() const { return 28 + code_size; }
    uint32_t pool_end() const { return pool_start() + pool_size; }
    uint32_t ctx28_start() const { return pool_end() + 4u * local_count; }
    uint32_t global_start() const { return ctx28_start() + 4u * ctx28_count; }
};
enum class Table : uint8_t { Frame, Local, Global, Ctx28 };
struct Reference {
    uint32_t file_offset = 0, target = 0;
    uint8_t width = 0;
    bool pool = false;
};
struct InlineControl { uint32_t file_offset = 0; uint16_t index = 0; uint8_t opcode = 0; };
struct Command {
    uint32_t pc = 0, size = 0;
    int opcode = -1;             // -1: linked switch metadata, not an opcode
    std::vector<Reference> references;
    std::vector<InlineControl> controls;
};

class Program {
public:
    // Only the five immutable original script identities are accepted.
    // Failed loads clear the program; error contains exact parser location.
    bool load_original(const std::filesystem::path& path, Diagnostic& error);
    bool load_original_bytes(std::vector<uint8_t> bytes, const std::filesystem::path& source,
                             Diagnostic& error);
    const Header& header() const { return header_; }
    const Source& source() const { return source_; }
    const std::vector<uint8_t>& bytes() const { return bytes_; }
    const std::vector<Command>& commands() const { return commands_; }
    const std::vector<Reference>& references() const { return references_; }
    const Command* command(uint32_t pc) const;
    uint32_t operand(const Command& command, uint32_t relative, uint8_t width) const;
    uint32_t initial_word(Table table, uint16_t index) const;
    std::string pool_bytes(uint32_t relative) const;
    bool loaded() const { return !bytes_.empty(); }
private:
    Header header_;
    Source source_;
    std::vector<uint8_t> bytes_;
    std::vector<Command> commands_;
    std::vector<Reference> references_;
    std::vector<uint32_t> command_index_;
    void parse();
};

struct DictionaryRecord {
    uint32_t index = 0, file_offset = 0, end = 0;
    std::array<uint32_t, 20> words{}; // opaque metadata is preserved
    std::array<uint32_t, 3> text_offsets{};
    std::string written_cp932, reading_cp932, alternate_cp932, initial_cp932;
};
class Dictionary {
public:
    bool load_original(const std::filesystem::path& path, Diagnostic& error);
    // Data-only counterpart to the file adapter; original five hash pins and
    // exact record validation still apply. source is provenance metadata.
    // Failure clears records/source and retains the diagnostic.
    bool load_original_bytes(std::vector<uint8_t> bytes, const std::filesystem::path& source,
                             Diagnostic& error);
    const Source& source() const { return source_; }
    const std::vector<DictionaryRecord>& records() const { return records_; }
private:
    Source source_;
    std::vector<DictionaryRecord> records_;
};

struct Value {
    enum class Kind { Unknown, Scalar, String, Code, Reference, Opaque };
    Kind kind = Kind::Unknown;
    uint32_t word = 0;          // preserve original tag/payload for scalar/opaque
    Table table = Table::Frame;
    uint16_t index = 0;
    std::string cp932;
    static Value integer(int32_t value);
    static Value string(std::string cp932);
    int32_t signed_integer() const;
};

enum class Status { Running, Message, Stopped };
struct Event {
    Status status = Status::Stopped;
    uint32_t pc = 0, next_pc = 0, file_offset = 0;
    int opcode = -1;
    bool branch_taken = false;
    bool has_branch_decision = false;
    std::string message_cp932;  // expanded request from executed opcode 48 only
    uint32_t display_state = 0; // observed ctx+34 = 6 for opcode 48
    uint16_t display_count = 0; // observed ctx+56 increments
    Diagnostic diagnostic;
};

class Vm {
public:
    explicit Vm(const Program& program) : program_(program) {}
    // No default argument values are invented. Unprovided slots are Unknown.
    // Startup argument ABI/caller is not recovered; start is a restricted probe.
    Event start(const std::vector<Value>& arguments = {});
    // Isolated original function fixture, NOT a normal gameplay entry point.
    Event start_at(uint32_t code_pc, const std::vector<Value>& arguments);
    Event step();
    Event run(uint32_t instruction_budget = 1000);
    const std::vector<Value>& stack() const { return stack_; }
    uint32_t pc() const { return pc_; }
    uint32_t display_state() const { return display_state_; }
    uint16_t display_count() const { return display_count_; }
    bool stopped() const { return stopped_; }
    Value read_slot(Table table, uint16_t index) const;
private:
    const Program& program_; // program must outlive Vm and remain unchanged
    std::vector<Value> stack_, frame_, local_, global_, ctx28_;
    uint32_t pc_ = 0, display_state_ = 0;
    uint16_t display_count_ = 0;
    bool stopped_ = true, frame_reserved_ = false;
    Diagnostic last_error_;
    Value decode(uint32_t word) const;
    std::vector<Value>& slots(Table table);
    std::string expand(const Command& command) const;
    Event stop(const Command* command, const std::string& reason);
};
} // namespace dokodemo::script
