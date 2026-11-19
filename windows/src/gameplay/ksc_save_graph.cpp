#include "gameplay/ksc_save_graph.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {
namespace {
constexpr std::size_t capacity = 0x40000 - 0x10b50;
constexpr unsigned max_depth = 64; // Host stack protection.
void Require(bool valid, const char* reason) {
    if (!valid) throw std::invalid_argument(reason);
}
std::size_t Count(std::uint32_t word) {
    Require(!(word & 0x2000), "Negative save array size");
    return word & 0x3fff;
}
bool Scalar(unsigned tag) {
    return tag <= 4 || tag == 7;
}
struct Writer {
    std::vector<std::uint8_t> data;
    std::size_t Reserve(std::size_t n) {
        const auto at = data.size();
        Require(n <= capacity-at, "Save graph exceeds slot");
        data.resize(at+n);
        return at;
    }
    void Word(std::size_t at, std::uint32_t word) {
        for (unsigned i=0;i<4;++i) data[at+i]=static_cast<std::uint8_t>(word>>(8*i));
    }
    void Put(const KscSaveValue& value, std::size_t at, unsigned depth, bool object_start) {
        Require(depth < max_depth, "Save graph depth exceeded");
        const auto tag = value.word >> 28;
        // 7CAA4 compares the word cursor with the append cursor on entry
        // to an array. Only the root or a newly reached object starts there.
        if (tag==9) Require(object_start,"Inline save array");
        if (object_start) Require(Scalar(tag) || tag==9,"Unsupported save object body");
        if (tag != 5 && tag != 6) Require(value.text.empty(),"Unexpected save text");
        if (tag != 9 && tag != 11) Require(value.children.empty(),"Unexpected save children");
        if (Scalar(tag)) {
            Word(at,value.word);
        } else if (tag == 5 || tag == 6) {
            Require(value.text.find('\0') == std::string::npos && value.text.size() < capacity, "Invalid save string");
            auto dest = Reserve(value.text.size()+1);
            Word(at,0x60000000u | static_cast<std::uint32_t>(dest));
            std::copy(value.text.begin(),value.text.end(),data.begin()+dest);
        } else if (tag == 9) {
            Require(data.size()==at+4 && value.children.size()==Count(value.word),"Invalid save array placement");
            Word(at,value.word);
            auto dest=Reserve(value.children.size()*4);
            for (const auto& child:value.children) {
                Put(child,dest,depth+1,false);
                dest+=4;
            }
        } else if (tag == 11) {
            Require(value.children.size()==1,"Save reference requires its live object");
            const auto dest=Reserve(4);
            Word(at,0xb0000000u | static_cast<std::uint32_t>(dest));
            Put(value.children.front(),dest,depth+1,true);
        } else throw std::invalid_argument("Unsupported save value tag");
    }
};
struct Reader {
    const std::uint8_t* data;
    std::size_t limit, tail=4;
    KscSaveValue Get(std::size_t at, unsigned depth, bool object_start) {
        Require(depth < max_depth && at<=limit && limit-at>=4,"Invalid save word");
        KscSaveValue value;
        for (unsigned i=0;i<4;++i) value.word |= std::uint32_t(data[at+i])<<(8*i);
        const auto tag=value.word>>28;
        if (object_start) Require(Scalar(tag) || tag==9,"Unsupported packed object body");
        if (Scalar(tag)) return value;
        if (tag==9) {
            const auto count=Count(value.word);
            Require(object_start && tail==at+4 && count<=(limit-tail)/4,"Invalid packed array");
            tail+=4*count;
            value.children.reserve(count);
            for (std::size_t i=0;i<count;++i) value.children.push_back(Get(at+4*(i+1),depth+1,false));
        } else if (tag==6 || tag==11) {
            const auto offset=value.word & 0x0fffffffu;
            // 7CA30 and 7CB48 use the current append cursor relative to base.
            Require(offset==tail && offset<limit,"Invalid packed reference");
            value.word=tag<<28;
            if (tag==6) {
                const auto* end=std::find(data+tail,data+limit,0);
                Require(end!=data+limit,"Unterminated save string");
                value.text.assign(reinterpret_cast<const char*>(data+tail),end-(data+tail));
                tail=std::size_t(end-data)+1;
            } else {
                Require(limit-tail>=4,"Missing save reference body");
                tail+=4;
                value.children.push_back(Get(offset,depth+1,true));
            }
        } else throw std::invalid_argument("Unsupported packed save tag");
        return value;
    }
};
}

std::vector<std::uint8_t> EncodeKscSaveGraph(const KscSaveValue& root) {
    Require(root.word>>28 == 9, "Save root must be an array");
    Writer w;
    w.Reserve(4);
    w.Put(root,0,0,true);
    return std::move(w.data);
}
KscSaveValue DecodeKscSaveGraph(const std::uint8_t* data, std::size_t length, std::size_t& used) {
    Require(data && length>=4,"Missing save root");
    Reader r{data,std::min(capacity,length)};
    auto result=r.Get(0,0,true);
    Require(result.word>>28 == 9, "Packed save root must be an array");
    used=r.tail;
    return result;
}
} // namespace dokodemo::gameplay
