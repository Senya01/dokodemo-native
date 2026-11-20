#include "room_fallback_list.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace dokodemo::gameplay {

bool RoomFallbackList::Load(const std::filesystem::path& dir,std::string& error,unsigned actor) try {
    struct Layout { const char* filename; unsigned ordinary, partner; };
    constexpr Layout layouts[]{
        {"toro-fallback-list.bin",1057,48}, {"robo-fallback-list.bin",512,40},
        {"usagi-fallback-list.bin",490,33}, {"inu-fallback-list.bin",512,31}, {"kaeru-fallback-list.bin",547,30},
    };
    if (actor>=std::size(layouts)) throw std::invalid_argument("Unknown procedure selector actor");
    const auto& layout=layouts[actor];
    std::ifstream in(dir/layout.filename,std::ios::binary);
    auto word=[&]() {
        std::array<unsigned char,4> b{};
        if (!in.read(reinterpret_cast<char*>(b.data()),4)) throw std::runtime_error("Short procedure selector data");
        return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);
    };
    if (word()!=0x324c4652) throw std::runtime_error("Invalid procedure selector package");
    auto read=[&](Selector& selector, unsigned list_size, unsigned ring_size) {
        if (word()!=(0x90000000u|list_size)) throw std::runtime_error("Invalid procedure list");
        for (unsigned i=0;i<list_size;++i) {
            const auto entry=word();
            if (entry>>28!=7) throw std::runtime_error("Expected a KSC procedure");
            selector.entries.push_back(entry);
        }
        if (word()!=(0x90000000u|ring_size)) throw std::runtime_error("Invalid initial procedure history");
        for (unsigned i=0;i<ring_size;++i) {
            const auto initial=word();
            if (initial) throw std::runtime_error("Unexpected initial mode-3 history cell");
            selector.history.push_back(initial);
        }
        selector.Refresh();
    };
    RoomFallbackList next;
    read(next.ordinary_,layout.ordinary,100);
    read(next.partner_,layout.partner,20);
    if (in.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Trailing selector data");
    *this=std::move(next);
    error.clear();
    return true;
} catch(const std::exception& ex) {error=ex.what();return false;}

void RoomFallbackList::Selector::Refresh() {
    constexpr unsigned payload=0x0fffffff;
    std::unordered_set<unsigned> prior;
    for (const auto value:history) prior.insert(value&payload);
    eligible.clear();
    for (unsigned i=0;i<entries.size();++i)
        if (!prior.count(entries[i]&payload)) eligible.push_back(i);
    // BOOT 77404 uses the whole list if exclusion removed every entry.
    if (eligible.empty())
        for (unsigned i=0;i<entries.size();++i) eligible.push_back(i);
}

unsigned RoomFallbackList::Selector::Consume(unsigned rank) {
    const auto word=entries.at(eligible.at(rank));
    history.at(cursor)=word;
    cursor=(cursor+1)%static_cast<unsigned>(history.size());
    Refresh();
    return word&0x0fffffff;
}

KscSaveValue RoomFallbackList::Selector::SaveTable() const {
    if (history.empty()) throw std::logic_error("Missing source procedure history");
    KscSaveValue body{0x90000000u | (cursor<<14) | static_cast<unsigned>(history.size()),{},{}};
    for (auto word:history) body.children.push_back({word,{},{}});
    return {0xb0000000u,{}, {std::move(body)}};
}

void RoomFallbackList::Selector::Restore(const KscSaveValue& table) {
    if (table.word!=0xb0000000u || !table.text.empty() || table.children.size()!=1)
        throw std::invalid_argument("Invalid procedure history reference");
    const auto& body=table.children.front();
    const auto next_cursor=(body.word>>14)&0x3fffu;
    if ((body.word>>28)!=9 || (body.word&0x3fffu)!=history.size() || body.children.size()!=history.size() ||
        !body.text.empty() || next_cursor>=history.size())
        throw std::invalid_argument("Invalid procedure ring header");
    Selector next=*this;
    for (size_t i=0; i<history.size(); ++i) {
        const auto& cell=body.children[i];
        if (!cell.children.empty() || !cell.text.empty() ||
            (cell.word && std::find(entries.begin(),entries.end(),cell.word)==entries.end()))
            throw std::invalid_argument("History word does not belong to its procedure list");
        next.history[i]=cell.word;
    }
    next.cursor=next_cursor;
    next.Refresh();
    *this=std::move(next);
}
}
