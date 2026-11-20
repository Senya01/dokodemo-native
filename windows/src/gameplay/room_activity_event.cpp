#include "room_activity_event.h"
#include "core_hash.h"
#include "recovered/rng_native.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {
namespace {
constexpr unsigned outing_threshold = 20; // G59C2[1] in the initial script data.
constexpr unsigned message_threshold = 10, message_interval = 10;
constexpr unsigned word_integer = 0x20000000, word_time = 0x30000000, payload_mask = 0x0fffffff;
unsigned Bcd(unsigned value) { return (value / 10) * 16 + value % 10; }
RoomEventSelection Boundary(unsigned pc) {
    RoomEventSelection out;
    out.boundary_pc = pc;
    return out;
}
RoomEventSelection Routine(unsigned pc) {
    RoomEventSelection out;
    out.topic_pc = pc;
    return out;
}
}

bool RoomTopicTable::Load(const std::filesystem::path& directory, Actor actor,
                          std::uint32_t packed_birthday, std::string& error) {
    *this = {};
    struct Source { const char* name; unsigned bytes; const char* sha; };
    constexpr Source sources[]{
        {"toro-room-events.bin",1162,"a88494f899e99609c6df34e98ffd346fcada50b5fdf79ec2003899650437eaa8"},
        {"robo-room-events.bin",820,"46efc23442618d7858106d55ccf253132fa9ea0135f3cfa558cf317b20fffb94"},
        {"usagi-room-events.bin",1014,"34003b3c31833483e151cbc220c8a076cd8122659599676e30313c77b50c14a4"},
        {"inu-room-events.bin",1113,"487d078e368ce2cb854451c5f7f078837e88ca036f429da35955b58b80da1991"},
        {"kaeru-room-events.bin",910,"69be4b6eb75abfe58e6f5a5b53c439478a5b5efe4f38f8bdf0d6b7438162eefb"},
    };
    if (unsigned(actor)>=std::size(sources)) { error="Unknown conversation catalog actor"; return false; }
    const auto& source=sources[unsigned(actor)];
    std::ifstream in(directory / source.name, std::ios::binary | std::ios::ate);
    std::vector<unsigned char> bytes(source.bytes);
    if (!in || in.tellg() != static_cast<std::streamoff>(bytes.size()) ||
        !in.seekg(0).read(reinterpret_cast<char*>(bytes.data()), bytes.size()) ||
        core::sha256_hex(bytes.data(), bytes.size()) != source.sha) {
        error = std::string("Missing or incorrect ordinary topic list: ") + source.name;
        return false;
    }
    std::size_t offset = 4;
    const auto read = [&](unsigned width) {
        unsigned value = 0;
        for (unsigned i = 0; i < width; ++i)
            value |= unsigned(bytes.at(offset++)) << (i * 8);
        return value;
    };
    const auto count = read(2);
    capacity_ = read(2);
    for (unsigned i = 0; i < count; ++i) {
        Row row{read(4), {}};
        const auto choices = read(1);
        for (unsigned j = 0; j < choices; ++j)
            row.entries.push_back(read(4));
        rows_.push_back(std::move(row));
    }
    // The game imports GAME20's profile and writes the low sixteen
    // bits of the adopted birthday into the first callback row.
    rows_.front().category = packed_birthday & 0xffff;
    return true;
}

bool RoomActivityEvent::Load(const std::filesystem::path& directory, std::uint32_t packed_birthday,
                             std::string& error,RoomTopicTable::Actor actor) {
    *this = {};
    if (!topic_table_.Load(directory, actor, packed_birthday, error))
        return false;
    stage_ = Stage::introduction;
    return true;
}

KscSaveValue RoomActivityEvent::ContactCountSaveWord() const {
    return {0xb0000000u, {}, {{word_integer | total_contacts_, {}, {}}}};
}

void RoomActivityEvent::ValidateContactCount(const KscSaveValue& word) {
    if (word.word >> 28 != 11 || !word.text.empty() || word.children.size()!=1)
        throw std::invalid_argument("Missing G59CC integer reference");
    const auto& count = word.children.front();
    if (count.word >> 28 != 2 || !count.text.empty() || !count.children.empty())
        throw std::invalid_argument("Invalid G59CC integer");
}

void RoomActivityEvent::RestoreContactCount(const KscSaveValue& word) {
    ValidateContactCount(word);
    total_contacts_ = word.children.front().word & payload_mask;
}

void RoomActivityEvent::CountContact() {
    // 89A28..89A43. C2 is reset when its outing timer fires. CC is cumulative.
    ++outing_count_;
    ++home_count_; // 89A30/89A33 -> 1E4C(G59BD)
    ++counted_contacts_;
    total_contacts_ = (total_contacts_ + 1) & payload_mask;
}

namespace {
KscSaveValue CounterTable(unsigned count,unsigned threshold,unsigned interval,std::optional<std::uint32_t> start) {
    const auto timer = start ? word_time | (*start & payload_mask) : word_integer;
    KscSaveValue array{0x90000004u, {}, {
        {word_integer | (count & payload_mask), {}, {}},
        {word_integer | threshold, {}, {}},
        {word_integer | interval, {}, {}},
        {timer, {}, {}}
    }};
    return {0xb0000000u, {}, {std::move(array)}};
}

void ValidateCounter(const KscSaveValue& table,unsigned threshold) {
    if (table.word >> 28 != 11 || !table.text.empty() || table.children.size()!=1)
        throw std::invalid_argument("Missing room array reference");
    const auto& array=table.children.front();
    if (array.word!=0x90000004u || !array.text.empty() || array.children.size()!=4)
        throw std::invalid_argument("Invalid room counter shape");
    const auto& cells=array.children;
    for (const auto& cell:cells)
        if (!cell.children.empty() || !cell.text.empty())
            throw std::invalid_argument("Non-scalar room counter field");
    if ((cells[0].word & 0xf8000000u)!=word_integer ||
        cells[1].word!=(word_integer | threshold) ||
        (cells[2].word & 0xf8000000u)!=word_integer ||
        (cells[3].word >> 28 != 3 && cells[3].word!=word_integer))
        throw std::invalid_argument("Unsupported room count, threshold, interval or time");
}

int SignedMinute(std::uint32_t value) {
    const int n=static_cast<int>(value & payload_mask);
    return (n & 0x08000000) ? n-0x10000000 : n;
}

// 1DDF uses two minutes per excess contact, limited to a third of the interval.
bool CounterDue(unsigned count,unsigned threshold,unsigned interval,std::uint32_t start,std::uint32_t now) {
    if (count < threshold) return false;
    const unsigned reduction=std::min((count-threshold)*2,interval/3);
    return SignedMinute(now)-SignedMinute(start) >= static_cast<int>(interval-reduction);
}
} // namespace

KscSaveValue RoomActivityEvent::HomeCounterSaveTable() const {
    return CounterTable(home_count_,outing_threshold,home_interval_,home_timer_start_);
}
void RoomActivityEvent::ValidateHomeCounter(const KscSaveValue& table) {
    ValidateCounter(table,outing_threshold);
}
void RoomActivityEvent::RestoreHomeCounter(const KscSaveValue& table) {
    ValidateHomeCounter(table);
    const auto& cells=table.children.front().children;
    home_count_=cells[0].word&payload_mask;
    home_interval_=cells[2].word&payload_mask;
    if(cells[3].word==word_integer) home_timer_start_.reset();
    else home_timer_start_=cells[3].word&payload_mask;
}
KscSaveValue RoomActivityEvent::OutingCounterSaveTable() const {
    return CounterTable(outing_count_,outing_threshold,outing_interval_,outing_timer_start_);
}
void RoomActivityEvent::ValidateOutingCounter(const KscSaveValue& table) {
    ValidateCounter(table,outing_threshold);
}
KscSaveValue RoomActivityEvent::MessageCounterSaveTable() const {
    return CounterTable(counted_contacts_,message_threshold,message_interval,message_timer_start_);
}
void RoomActivityEvent::ValidateMessageCounter(const KscSaveValue& table) {
    ValidateCounter(table,message_threshold);
    if (table.children.front().children[2].word!=(word_integer | message_interval))
        throw std::invalid_argument("Unsupported G59C7 message interval");
}
void RoomActivityEvent::RestoreMessageCounter(const KscSaveValue& table) {
    ValidateMessageCounter(table);
    const auto& cells=table.children.front().children;
    counted_contacts_=cells[0].word & payload_mask;
    if (cells[3].word==word_integer) message_timer_start_.reset();
    else message_timer_start_=cells[3].word & payload_mask;
}

void RoomActivityEvent::RestoreOutingCounter(const KscSaveValue& table) {
    ValidateOutingCounter(table);
    const auto& cells=table.children.front().children;
    outing_count_=cells[0].word & payload_mask;
    outing_interval_=cells[2].word & payload_mask;
    if (cells[3].word==word_integer)
        outing_timer_start_.reset();
    else
        outing_timer_start_=cells[3].word & payload_mask;
}

void RoomActivityEvent::StartActorScript(std::uint32_t minute) {
    if(stage_!=Stage::introduction || outing_timer_start_)
        throw std::logic_error("Unexpected outing timer initialization");
    // 8D40D calls 1E74(G59C2,30): fields 0/2/3 receive count, interval, clock.
    home_count_=0;
    home_timer_start_=minute; // 8D403 -> 1E74(G59BD,10)
    home_interval_=10;
    outing_timer_start_=minute;
    counted_contacts_=0;
    message_timer_start_=minute; // 8D417 -> 1E74(G59C7,10)
    messages_.ResetActor(); // Fresh G408F and G59CE; 8D45F clears the diary row.
}

void RoomActivityEvent::ResetMessageDay(std::uint32_t minute) {
    // 1C43/1C47 run together when 1B34 changes the registered day.
    messages_.Clear();
    counted_contacts_=0;
    message_timer_start_=minute;
}

void RoomActivityEvent::ResumeDay(std::uint32_t registered_minute) {
    if (stage_!=Stage::introduction) throw std::logic_error("Unexpected resumed topic dispatcher");
    first_day_=registered_minute/1440;
    stage_=Stage::idle;
}

void RoomActivityEvent::FinishIntroduction(std::uint32_t minute) {
    if (stage_ == Stage::introduction) {
        cleanup_minute_ = minute;    // 8D940..8D947 sets ctx28[E1F2].
        first_day_ = minute / 1440;
        CountContact();               // Cold 898A2..898A6 -> 89961(true).
        stage_ = Stage::idle;
    }
}

RoomEventSelection RoomTopicTable::Select(unsigned key, bool mask, recovered::RngNativeOwner& rng) {
    RoomEventSelection result;
    // NEKO 88A23 and ROBO 665DE both use an eight-slot frame. Builtin 60
    // fills it in source order and the caller clears only direct code rows.
    for (unsigned i = 0; i < rows_.size(); ++i) {
        const auto& row = rows_[i];
        if (!row.present || (mask ? (row.category & key) != key : row.category != key)) continue;
        result.eligible_rows.push_back(i);
        if (result.eligible_rows.size() == capacity_) break;
    }
    if (result.eligible_rows.empty()) return result;
    const auto selected = result.eligible_rows[rng.NextC3234() % result.eligible_rows.size()];
    result.selected_row = selected;
    auto& row = rows_[selected];
    if (row.entries.size() == 1) {
        row.present = false;
        result.topic_pc = row.entries.front();
    } else {
        // NEKO 89994 and ROBO 678B1 resolve indirect choices with F6 mode 0.
        result.topic_pc = row.entries[rng.NextC3234() % row.entries.size()];
    }
    return result;
}

RoomEventSelection RoomActivityEvent::SelectCatalogTopic(unsigned key, bool mask, recovered::RngNativeOwner& rng) {
    return topic_table_.Select(key, mask, rng);
}

RoomEventSelection RoomActivityEvent::Choose(unsigned key, bool mask, recovered::RngNativeOwner& rng) {
    auto result = SelectCatalogTopic(key, mask, rng);
    if (!result.topic_pc) return result;
    pending_topic_ = result.topic_pc;
    return result;
}

void RoomActivityEvent::HomeInvitationReturned() {
    if (!home_offer_mode_ || pending_topic_!=0x12f39)
        throw std::logic_error("No selected home offer has returned");
    const auto destination=location_.current_destination_id ? location_.current_destination_id() : location_.descriptor_id;
    if (!destination) {
        // 89C79/89C21, before the caller starts its farewell: 1E55(timer,5), then [0]=[1]-3.
        auto& count=*home_offer_mode_ ? outing_count_ : home_count_;
        auto& interval=*home_offer_mode_ ? outing_interval_ : home_interval_;
        auto& start=*home_offer_mode_ ? outing_timer_start_ : home_timer_start_;
        count=0;
        start=CurrentMinute();
        interval=5;
        count=outing_threshold-3;
    }
    home_offer_mode_.reset();
}

unsigned RoomActivityEvent::FinishConversation(std::uint32_t minute, bool count_contact) {
    if (stage_ == Stage::direct_menu) {
        // These selected menu scripts return to 8E023 -> 8D932(false).
        pending_topic_.reset();
        cleanup_minute_ = minute;
        stage_ = Stage::idle;
        return 0;
    }
    if (!pending_topic_ && !awaiting_cleanup_)
        throw std::logic_error("No completed ordinary or teaching callback to retire");
    if (home_offer_mode_) throw std::logic_error("Invitation farewell preceded the source return");
    if (hourly_topic_) hour_topic_after_ = minute_ + 180; // 89D4C uses the caller's original minute.
    pending_topic_.reset();
    awaiting_cleanup_ = false;
    hourly_topic_ = false;
    return CompleteContact(minute,count_contact);
}

unsigned RoomActivityEvent::FinishScheduledEvent(std::uint32_t minute, bool count_contact) {
    if (stage_ != Stage::idle || pending_topic_ || awaiting_cleanup_)
        throw std::logic_error("Scheduled event cannot retire a different conversation");
    return CompleteContact(minute,count_contact);
}

unsigned RoomActivityEvent::CompleteContact(std::uint32_t minute, bool count_contact) {
    if (count_contact) {
        CountContact();
        if (counted_contacts_>=message_threshold && !message_timer_start_) {
            stage_=Stage::terminal;
            return 0x1ddf; // Reached a timer without an initialized source clock.
        }
        if (message_timer_start_ && CounterDue(counted_contacts_,message_threshold,message_interval,
                                                *message_timer_start_,minute)) {
            // 89A51 resets before 89A5F, including when A496 returns false.
            counted_contacts_=0;
            message_timer_start_=minute;
            if (const auto boundary=messages_.ContactSelectionBoundary()) {
                stage_=Stage::terminal;
                return boundary;
            }
        }
    }
    cleanup_minute_ = minute;
    stage_ = Stage::idle;
    return 0;
}

std::optional<RoomEventSelection> RoomActivityEvent::Begin(std::uint32_t minute, bool teaching) {
    if (stage_ != Stage::idle) throw std::logic_error("Room conversation already owned");
    teaching_ = teaching;
    stage_ = Stage::terminal;
    if (minute / 1440 != first_day_) return Boundary(0x1b34);
    if (std::int64_t(minute) - cleanup_minute_ >= 5) {
        auto preface = Routine(0x7d4cc);
        preface.greeting = true;
        stage_ = Stage::greeting;
        return preface;
    }
    return std::nullopt;
}

RoomEventSelection RoomActivityEvent::Teach(const std::function<void()>& begin_vocabulary) {
    if (begin_vocabulary) begin_vocabulary();
    stage_ = Stage::terminal;
    pending_topic_ = 0x8dba; // 8DF38 -> 899BB, 8DF3C calls the teaching routine.
    return Routine(*pending_topic_);
}

RoomEventSelection RoomActivityEvent::SelectTeaching(std::uint32_t minute,
                                                     const std::function<void()>& begin_vocabulary) {
    if (auto preface = Begin(minute, true)) return *preface;
    return Teach(begin_vocabulary);
}

RoomEventSelection RoomActivityEvent::SelectFriendsAction(unsigned argument) {
    if (stage_ != Stage::idle) throw std::logic_error("Room conversation already owned");
    switch (argument) {
    case 2: pending_topic_ = 0x10c9b; break;
    case 7: pending_topic_ = 0x8b0bd; break;
    case 8: pending_topic_ = 0x8b10b; break;
    case 9: pending_topic_ = 0x8b0e2; break;
    default: throw std::logic_error("Unknown friends menu argument");
    }
    stage_ = Stage::direct_menu;
    return Routine(*pending_topic_);
}

RoomEventSelection RoomActivityEvent::SelectOuting(bool return_home) {
    if (stage_ != Stage::idle) throw std::logic_error("Room conversation already owned");
    stage_ = Stage::direct_menu;
    pending_topic_ = return_home ? 0x1320e : 0x13219; // 8DF81..8DF88 handles event argument 3 directly.
    return Routine(*pending_topic_);
}

RoomEventSelection RoomActivityEvent::SelectConversation(unsigned month, unsigned day, std::uint32_t minute,
                                                        recovered::RngNativeOwner& rng,
                                                        const std::function<std::optional<bool>()>& first_contact_special,
                                                        const std::function<void()>& begin_vocabulary,
                                                        RoomContactLocation location) {
    if(stage_!=Stage::idle) throw std::logic_error("Room conversation already owned");
    if(location.scene!=location.kind || (location.scene!=0)!=(location.descriptor_id!=0))
        throw std::logic_error("Conversation location does not match its script descriptor");
    location_=location; location_check_=0;
    if (auto preface = Begin(minute, false)) return *preface;
    return Dispatch(month, day, minute, rng, first_contact_special, begin_vocabulary);
}

RoomEventSelection RoomActivityEvent::ResumeAfterGreeting(unsigned month, unsigned day, std::uint32_t minute,
                                                         recovered::RngNativeOwner& rng,
                                                         const std::function<std::optional<bool>()>& first_contact_special,
                                                         const std::function<void()>& begin_vocabulary) {
    if (stage_ != Stage::greeting) throw std::logic_error("No pending 7D4CC greeting return");
    if (teaching_) return Teach(begin_vocabulary);
    return Dispatch(month, day, minute, rng, first_contact_special, begin_vocabulary);
}

RoomEventSelection RoomActivityEvent::Dispatch(unsigned month, unsigned day, std::uint32_t minute,
                                              recovered::RngNativeOwner& rng,
                                              const std::function<std::optional<bool>()>& first_contact_special,
                                              const std::function<void()>& begin_vocabulary) {
    if (!month || month > 12 || !day || day > 31) throw std::logic_error("Invalid room calendar date");
    if (begin_vocabulary) begin_vocabulary(); // 899ED calls 0FD1 before 89E23.
    minute_ = minute;
    month_ = month;
    day_ = day;
    if (auto queued = pending_callbacks_.Pop(rng)) {
        stage_ = Stage::queued;
        pending_topic_ = queued;
        auto result = Routine(*queued);
        result.queued = true;
        return result;
    }
    return AfterQueue(rng, first_contact_special);
}

RoomEventSelection RoomActivityEvent::AfterQueue(recovered::RngNativeOwner& rng,
                                                const std::function<std::optional<bool>()>& first_contact_special) {
    // 89E2F follows a false 2573 return. On this first-day path, day 1
    // differs from 59A4 - 1 (13), and 2876 is nil, so 89A8A returns false.
    if (rng.NextC3234() % 100 <= 40) {
        stage_ = Stage::word;
        if (first_contact_special && first_contact_special() == false)
            return ResumeAfterEmptyWordLookup(rng);
        return Boundary(0x10b3d);
    }
    stage_ = Stage::calendar;
    return Continue(rng);
}

RoomEventSelection RoomActivityEvent::ResumeAfterEmptyWordLookup(recovered::RngNativeOwner& rng) {
    if (stage_ != Stage::word) throw std::logic_error("No pending ordinary word lookup");
    stage_ = Stage::calendar;
    return Continue(rng);
}

RoomEventSelection RoomActivityEvent::ResumeAfterRejectedTopic(unsigned returned_topic,
                                                             recovered::RngNativeOwner& rng,
                                                             const std::function<std::optional<bool>()>& first_contact_special) {
    if (pending_topic_ != returned_topic) throw std::logic_error("Unmatched ordinary topic return");
    if (stage_ == Stage::direct_menu) throw std::logic_error("Selected menu script returns to its own caller");
    if (teaching_) throw std::logic_error("Teaching callback returns to its own farewell");
    if (home_offer_mode_) throw std::logic_error("Selected home invitation returns true");
    pending_topic_.reset();
    hourly_topic_ = false;
    if (stage_ == Stage::queued) return AfterQueue(rng, first_contact_special);
    if (returned_topic == 0x7d43a) {
        RoomEventSelection out;
        awaiting_cleanup_ = true;
        out.finished = true;
        return out;
    }
    return Continue(rng);
}

std::uint32_t RoomActivityEvent::CurrentMinute() const {
    return location_.current_minute ? location_.current_minute() : minute_;
}

RoomEventSelection RoomActivityEvent::CheckHomeOffers(recovered::RngNativeOwner& rng) {
    const auto stayed_one_minute=[&] {
        return SignedMinute(CurrentMinute())-SignedMinute(*location_.transfer_minute)>=1;
    };
    if(home_count_>=outing_threshold && !home_timer_start_) return Boundary(0x1ddf);
    if(home_count_>=outing_threshold && home_timer_start_ && CounterDue(home_count_,outing_threshold,home_interval_,*home_timer_start_,CurrentMinute())
       && stayed_one_minute()) {
        // 89C61: Code38 returns 120 + builtin17(360), before 1E55 and 12F39(0).
        const unsigned interval=120+rng.NextC3234()%360;
        home_count_=0;
        home_timer_start_=CurrentMinute();
        home_interval_=interval;
        if(!location_.select_home_offer) return Boundary(0x12f39);
        if(location_.select_home_offer(0,rng)) {
            home_offer_mode_=0;
            pending_topic_=0x12f39;
            return Routine(*pending_topic_);
        }
    }
    // After a false 89C3C, 89EAE checks G59C2 even in the same contact.
    if(outing_count_>=outing_threshold && !outing_timer_start_) return Boundary(0x1ddf);
    if(outing_count_>=outing_threshold && outing_timer_start_ && CounterDue(outing_count_,outing_threshold,outing_interval_,*outing_timer_start_,CurrentMinute())
       && stayed_one_minute()) {
        if(!location_.select_home_offer) return Boundary(0x12f39);
        if(location_.select_home_offer(1,rng)) {
            home_offer_mode_=1;
            pending_topic_=0x12f39;
            return Routine(*pending_topic_);
        }
        // 89C2F: only a completed false 12F39(1) applies this short retry.
        outing_count_=0;
        outing_timer_start_=CurrentMinute();
        outing_interval_=5;
    }
    return {};
}

RoomEventSelection RoomActivityEvent::Continue(recovered::RngNativeOwner& rng) {
    if (stage_ == Stage::calendar) {
        stage_ = Stage::day;
        auto date = Choose((Bcd(month_) << 8) | Bcd(day_), false, rng); // 89C93, builtin 10
        if (date.topic_pc) return date;
    }
    if (stage_ == Stage::day) {
        stage_ = Stage::fallback;
        // 89E97 reads the actual descriptor's integer ID (not its kind).
        // A nonzero ID calls 89B85; it does not try the two home procedures.
        if(!location_.transfer_minute || !*location_.transfer_minute) {
            location_.transfer_minute=CurrentMinute();
            if(location_.stamp_transfer_minute) location_.stamp_transfer_minute(*location_.transfer_minute);
        }
        location_check_=location_.descriptor_id ? 0x89b85 : 0x89c3c;
        if(!location_.descriptor_id) {
            auto offer=CheckHomeOffers(rng);
            if(offer.topic_pc || offer.boundary_pc) {
                stage_=Stage::terminal;
                return offer;
            }
        }
        if(location_.descriptor_id && outing_count_>=outing_threshold) {
            if(!outing_timer_start_) return Boundary(0x1ddf);
            if(CounterDue(outing_count_,outing_threshold,outing_interval_,*outing_timer_start_,minute_)) {
                if(SignedMinute(minute_)-SignedMinute(*location_.transfer_minute)>=1) {
                    // 89BAD calls Code0; 1E55 resets the timer before 12C61.
                    const bool short_interval=rng.NextC3234()%5==0;
                    outing_interval_=(short_interval?30u:60u)+rng.NextC3234()%(short_interval?31u:240u);
                    outing_count_=0; outing_timer_start_=minute_;
                    stage_=Stage::terminal;
                    return Boundary(0x12c61); // receives CT243F[1][0], the current kind
                }
            }
        }
        // 89ED3..89EE3 chooses the day group (day / 3 + 1).
        if (rng.NextC3234() % 100 < 50) {
            auto topic = Choose(1, false, rng);
            if (topic.topic_pc) return topic;
        }
    }
    if (stage_ == Stage::fallback) {
        stage_ = Stage::terminal;
        const auto roll = rng.NextC3234() % 100; // 89F12
        RoomEventSelection topic;
        if (roll < 10) topic = Choose(1u << (16 + month_ - 1), true, rng);
        else if (roll < 20) {
            if (hour_topic_after_ < minute_) {
                topic = Choose(100 + minute_ % 1440 / 60 / 3, false, rng);
                hourly_topic_ = topic.topic_pc.has_value();
            }
        }
        else if (roll < 58) {
            pending_topic_ = roll < 23 ? 0x4e27 : 0x89acc;
            return Routine(*pending_topic_);
        }
        if (topic.topic_pc) return topic;
    }
    pending_topic_ = 0x7d43a;
    return Routine(*pending_topic_); // 89F81 fallback message list.
}
}
