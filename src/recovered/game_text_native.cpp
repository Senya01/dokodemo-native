#include "game_text_native.h"
#include "core/cp932.h"
#include "game_primitives.h"

namespace dokodemo::recovered {
namespace {
bool Input32Unit(std::string_view text,size_t& at,uint32_t& unit) noexcept{
    const auto first=uint8_t(text[at++]);unit=first;
    if(first<0x80)return (first>='0'&&first<='9')||(first>='A'&&first<='Z')||(first>='a'&&first<='z');
    if(at==text.size())return false;
    unit=(uint32_t(first)<<8)|uint8_t(text[at++]);
    return unit==0x815B||(unit>=0x829F&&unit<=0x82F1)
        ||(unit>=0x8340&&unit<=0x837E)||(unit>=0x8380&&unit<=0x8393);
}
}
bool GameInput32TextAllowed(std::string_view text) noexcept{
    for(size_t at=0;at<text.size();){uint32_t unit=0;if(!Input32Unit(text,at,unit))return false;}
    return true;
}
bool GameInput32Normalize(std::string_view text,std::string& auxiliary){
    std::string result;uint32_t count=0;
    for(size_t at=0;at<text.size();){
        uint32_t unit=0;if(!Input32Unit(text,at,unit)||++count>8)return false;
        if(unit>='a'&&unit<='z')unit-=0x20;
        else if(unit>=0x8340&&unit<=0x837E)unit-=0xA1;
        else if(unit>=0x8380&&unit<=0x8393)unit-=0xA2;
        if(result.empty()&&(unit==0x815B||unit==0x82C1))continue;
        if(unit>=0x100)result.push_back(char(unit>>8));
        result.push_back(char(unit&0xFF));
    }
    auxiliary=std::move(result);return true;
}
bool GameNameBuffer::Replace(std::string candidate, uint32_t cursor) {
    std::vector<size_t> offsets{0};
    for (size_t at = 0; at < candidate.size();) {
        const auto lead = uint8_t(candidate[at]);
        Glyph glyph;
        if (lead < 32 || lead == 127 ||
            !read_glyph(reinterpret_cast<const uint8_t *>(candidate.data() + at), candidate.size() - at,
                        glyph)) {
            error_ = "Incomplete/control source text";
            return false;
        }
        const auto part = std::string_view(candidate).substr(at, glyph.consumed);
        const auto checked = core::count_cp932(part);
        if (!checked.valid || checked.code_units != 1) {
            error_ = "CP932 character unsupported by source941C8 boundary";
            return false;
        }
        at += glyph.consumed;
        offsets.push_back(at);
    }
    if (offsets.size() - 1 > maximum_) {
        error_ = "Source decoder-unit limit exceeded";
        return false;
    }
    if (cursor >= offsets.size()) {
        error_ = "Source text cursor outside name";
        return false;
    }
    bytes_ = std::move(candidate);
    boundaries_ = std::move(offsets);
    cursor_ = cursor;
    error_.clear();
    return true;
}
bool GameNameBuffer::Assign(std::string_view initial, uint32_t maximum) {
    if (maximum == 0 || maximum > 40) {
        error_ = "Unsupported native name bound";
        return false;
    }
    const auto old = maximum_;
    maximum_ = maximum;
    if (!Replace(std::string(initial), 0)) {
        maximum_ = old;
        return false;
    }
    End();
    return true;
}
bool GameNameBuffer::Insert(std::string_view committed) {
    if (committed.empty())
        return true;
    // Validate the whole committed IME transaction before insertion; rejected
    // composition never partly changes the real name or slices a DBCS unit.
    GameNameBuffer fragment;
    if (!fragment.Assign(committed, maximum_)) {
        error_ = fragment.Error();
        return false;
    }
    auto candidate = bytes_;
    candidate.insert(boundaries_[cursor_], committed);
    return Replace(std::move(candidate), cursor_ + fragment.Units());
}
bool GameNameBuffer::Backspace() {
    if (!cursor_)
        return true;
    auto next = bytes_;
    next.erase(boundaries_[cursor_ - 1], boundaries_[cursor_] - boundaries_[cursor_ - 1]);
    return Replace(std::move(next), cursor_ - 1);
}
bool GameNameBuffer::Delete() {
    if (cursor_ == Units())
        return true;
    auto next = bytes_;
    next.erase(boundaries_[cursor_], boundaries_[cursor_ + 1] - boundaries_[cursor_]);
    return Replace(std::move(next), cursor_);
}
bool GameTextMailbox::Push(const GameTextEvent &event) noexcept {
    if (event.bytes > event.text.size())
        return false;
    if (count_ == events_.size()) {
        head_ = count_ = 0;
        context_ = event.context;
        resync_ = true;
        return false;
    }
    events_[(head_ + count_) % events_.size()] = event;
    ++count_;
    return true;
}
bool GameTextMailbox::Pop(GameTextEvent &event) noexcept {
    if (resync_) {
        resync_ = false;
        event = {};
        event.context = context_;
        event.kind = GameTextKind::Resync;
        return true;
    }
    if (!count_)
        return false;
    event = events_[head_];
    head_ = (head_ + 1) % uint32_t(events_.size());
    --count_;
    return true;
}
void GameTextKeyGate::Context(uint64_t id, bool focused) noexcept {
    if (id != context_ || focused != focused_) {
        context_ = id;
        focused_ = focused;
        keys_.fill(false);
        ++epoch_;
    }
}
void GameTextKeyGate::Focus(bool focused) noexcept {
    focused_ = focused;
    keys_.fill(false);
    ++epoch_;
}
bool GameTextKeyGate::KeyDown(uint32_t key, bool repeat) noexcept {
    ++epoch_;
    if (!context_ || !focused_ || key >= keys_.size() || (repeat && !keys_[key]))
        return false;
    keys_[key] = true;
    return true;
}
bool GameNameEntry::Begin(std::string_view initial, uint32_t limit) {
    if (state_.live && state_.source_state != 3)
        return false;
    GameNameBuffer next;
    if (!next.Assign(initial, limit))
        return false;
    buffer_ = std::move(next);
    const auto edits = state_.edits, rejects = state_.rejections, accepts = state_.confirmations,
               cancels = state_.cancellations;
    state_ = {};
    state_.live = true;
    state_.source_state = 0;
    state_.context = ++serial_;
    state_.initial = state_.source120 = std::string(initial);
    state_.maximum_units = limit;
    state_.edits = edits;
    state_.rejections = rejects;
    state_.confirmations = accepts;
    state_.cancellations = cancels;
    return true;
}
bool GameNameEntry::Apply(const GameTextEvent &event) {
    if (!state_.live || event.context != state_.context || state_.source_state == 3)
        return false;
    const auto reject = [&](const char *error) {
        state_.error = error;
        ++state_.rejections;
        return false;
    };
    if (event.kind == GameTextKind::FocusLost || event.kind == GameTextKind::Resync) {
        state_.focused = state_.composing = false;
        return true;
    }
    if (event.kind == GameTextKind::FocusGained) {
        state_.focused = true;
        return true;
    }
    if (state_.source_state != 1 || !state_.focused)
        return reject("Editor not accepting focused input");
    if (event.kind == GameTextKind::CompositionBegin) {
        state_.composing = true;
        return true;
    }
    if (event.kind == GameTextKind::CompositionEnd) {
        state_.composing = false;
        return true;
    }
    if (event.kind == GameTextKind::InvalidEncoding)
        return reject("Character cannot be represented by CP932/source font decoder");
    if (state_.composing && event.kind != GameTextKind::Commit)
        return reject("Finish IME composition before editor commands");
    bool changed = false;
    switch (event.kind) {
    case GameTextKind::Commit:
        if (event.bytes > event.text.size())
            return reject("Text event extent");
        if (!buffer_.Insert(std::string_view(event.text.data(), event.bytes)))
            return reject(buffer_.Error().c_str());
        changed = event.bytes != 0;
        break;
    case GameTextKind::Backspace:
        if (!buffer_.Backspace())
            return reject(buffer_.Error().c_str());
        changed = true;
        break;
    case GameTextKind::Delete:
        if (!buffer_.Delete())
            return reject(buffer_.Error().c_str());
        changed = true;
        break;
    case GameTextKind::Left:
        buffer_.Left();
        break;
    case GameTextKind::Right:
        buffer_.Right();
        break;
    case GameTextKind::Home:
        buffer_.Home();
        break;
    case GameTextKind::End:
        buffer_.End();
        break;
    case GameTextKind::Confirm:
        state_.source120 = buffer_.Bytes();
        state_.accepted = true;
        ++state_.confirmations;
        state_.source_state = 2;
        state_.focused = false;
        break;
    case GameTextKind::Cancel:
        state_.source120.clear();
        state_.cancelled = true;
        ++state_.cancellations;
        state_.source_state = 2;
        state_.focused = false;
        break;
    default:
        return reject("Unsupported native editor command");
    }
    if (changed)
        ++state_.edits;
    state_.error.clear();
    return true;
}
bool GameNameEntry::SetStaged(std::string_view value) {
    if (state_.source_state != 1 || !state_.focused || state_.composing)
        return false;
    if (!buffer_.Assign(value, state_.maximum_units))
        return Reject(buffer_.Error());
    ++state_.edits;
    state_.error.clear();
    return true;
}
void GameNameEntry::ControlCompleted(uint8_t mode) {
    if (!state_.live || mode != 2)
        return;
    if (state_.source_state == 0)
        state_.source_state = 1;
    else if (state_.source_state == 2) {
        state_.source_state = 3;
        state_.result_ready = true;
        // 22A78 result pointer is absent for cancel/empty. No source name is
        // published while the close animation is still running.
        state_.result = state_.source120;
    }
}
bool GameNameEntry::Expire(){
    if(!state_.live||state_.source_state!=1)return false;
    state_.source120.clear();state_.accepted=state_.cancelled=false;
    state_.composing=state_.focused=false;state_.source_state=2;return true;
}
bool GameInput32KeyboardActivity(const GameNameEntrySnapshot& state,const GameTextEvent& event) noexcept{
    if(!state.live||state.source_state!=1||!state.focused||event.context!=state.context||event.bytes>event.text.size())return false;
    switch(event.kind){
    case GameTextKind::Commit:case GameTextKind::Backspace:case GameTextKind::Delete:
    case GameTextKind::Left:case GameTextKind::Right:case GameTextKind::Home:case GameTextKind::End:
    case GameTextKind::Confirm:case GameTextKind::Cancel:case GameTextKind::InvalidEncoding:
    case GameTextKind::CompositionBegin:case GameTextKind::CompositionEnd:return true;
    default:return false;
    }
}
GameNameEntrySnapshot GameNameEntry::Snapshot() const {
    auto value = state_;
    value.staged = buffer_.Bytes();
    value.units = buffer_.Units();
    value.native_caret_units = buffer_.Cursor();
    return value;
}
const std::string *GameNameEntry::Result() const noexcept {
    return state_.live && state_.source_state == 3 && state_.result_ready && !state_.result.empty()
               ? &state_.result
               : nullptr;
}
void GameNameEntry::Retire() noexcept {
    state_.live = state_.focused = state_.composing = state_.result_ready = false;
    state_.source_state = 3;
    state_.initial.clear();
    state_.staged.clear();
    state_.source120.clear();
    state_.result.clear();
    state_.error.clear();
    buffer_.Clear();
}
} // namespace dokodemo::recovered
