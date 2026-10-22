#include "game_actor_dictionary_native.h"
#include "core/cp932.h"
#include <functional>
#include <set>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void Need(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
std::string Text(const PackageView& image,uint32_t at,uint64_t limit) {
    Need(at<limit,"Actor dictionary string offset out of bounds");
    std::string out;
    while(at<limit) {
        const auto c=image.ReadU8(at++);
        if(!c) {
            Need(core::count_cp932(out).valid,"Actor dictionary malformed CP932");
            return out;
        }
        out.push_back(char(c));
        Need(out.size()<=65536,"Actor dictionary string budget exceeded");
    }
    throw std::runtime_error("Actor dictionary unterminated string");
}
}
GameFsStatus GameActorDictionary::Attach(PackageView image) {
    Close(); state_={};
    try {
        const auto bytes=image.Describe().size;
        Need(bytes>=8&&bytes<=16*1024*1024,"Actor dictionary extent");
        const auto head=image.ReadLE32(0),count=head&0x0FFFFFFF;
        Need(head>>28==9&&count>0&&count<=65535&&4ull+4ull*count<=bytes,
             "Actor dictionary root/count");
        image_=std::move(image);state_.source_live=true;state_.bytes=uint32_t(bytes);state_.records=count;
        return {};
    } catch(const std::exception& e) { return {GameFsCode::MalformedDirectory,0,e.what()}; }
}
GameFsStatus GameActorDictionary::Import(script::Vm& vm,const script::Value& destination) {
    if(!state_.source_live||state_.imported||state_.closed)
        return {GameFsCode::Unavailable,0,"Actor dictionary import needs a live,unused handle"};
    // Validate/prepare the entire finite copy before publishing any mutation.
    // Unlike the original caller's ignored -1,this native adaptation reports
    // malformed/mismatched trees and preserves the old destination on failure.
    std::vector<std::pair<script::Value,script::Value>> writes;
    std::vector<std::pair<script::Value,script::Value>> old_values;
    uint32_t strings=0,values=0;
    std::set<uint32_t> path;
    try {
        const auto limit=image_.Describe().size;
        std::function<void(uint32_t,script::Value,uint32_t)> copy;
        copy=[&](uint32_t at,script::Value target,uint32_t depth) {
            Need(depth<=32&&++values<=1000000,"Actor dictionary tree budget/cycle");
            Need(uint64_t(at)+4<=limit,"Actor dictionary value outside backing");
            const auto word=image_.ReadLE32(at),tag=word>>28;
            if(tag==11) {
                Need(path.insert(at).second,"Actor dictionary cyclic file reference");
                const auto next=vm.load_reference(target);
                Need(next.kind==script::Value::Kind::Reference,"Actor dictionary destination reference shape");
                copy(word&0x0FFFFFFF,next,depth+1);path.erase(at);return;
            }
            script::Value value;value.word=word;
            if(tag==9) {
                const auto old=vm.load_reference(target);const auto count=word&0xFFF;
                Need(old.kind==script::Value::Kind::ArrayHeader&&
                     count<=(old.word&0x0FFFFFFF)&&uint64_t(at)+4ull*(count+1)<=limit,
                     "Actor dictionary destination array shape/count");
                value.kind=script::Value::Kind::ArrayHeader;writes.emplace_back(target,value);
                for(uint32_t i=0;i<count;++i)copy(at+4+4*i,vm.indexed_reference(target,i),depth+1);
            } else if(tag==6) {
                const auto old=vm.load_reference(target);
                Need(old.kind!=script::Value::Kind::Reference&&old.kind!=script::Value::Kind::ArrayHeader,
                     "Actor dictionary scalar/string destination shape");
                value=script::Value::string(Text(image_,word&0x0FFFFFFF,limit));
                ++strings;writes.emplace_back(target,std::move(value));
            } else {
                Need(tag<=3,"Unsupported actor dictionary value tag");
                const auto old=vm.load_reference(target);
                Need(old.kind!=script::Value::Kind::Reference&&old.kind!=script::Value::Kind::ArrayHeader,
                     "Actor dictionary scalar destination shape");
                value.kind=tag==3?script::Value::Kind::Timestamp:script::Value::Kind::Scalar;
                writes.emplace_back(target,std::move(value));
            }
        };
        copy(0,destination,0);
        std::vector<script::Value> records;
        for(uint32_t i=0;i<state_.records;++i) {
            auto record=vm.load_reference(vm.indexed_reference(destination,i));
            Need(record.kind==script::Value::Kind::Reference,"Actor dictionary record aliases not constructed");
            Need(vm.load_reference(record).kind==script::Value::Kind::ArrayHeader,"Actor dictionary record array");
            records.push_back(std::move(record));
        }
        for(const auto& write:writes)old_values.emplace_back(write.first,vm.load_reference(write.first));
        // Every target was checked during preparation;no allocations are made
        // in the table itself when these prebuilt owned values are moved in.
        for(auto& write:writes)vm.store_reference(write.first,std::move(write.second));
        records_=std::move(records);destination_={};state_.imported=true;state_.destination_index=destination.index;
        state_.copied_values=values;state_.copied_strings=strings;
        image_.Reset();state_.source_live=false; // source2F44/2E50 handle retirement
        const auto first=ReadRecord(vm,0);state_.first_written=first.first;state_.first_reading=first.second;
        return {};
    } catch(const std::exception& e) {
        for(auto& old:old_values)vm.store_reference(old.first,std::move(old.second));
        records_.clear();state_.imported=false;
        return {GameFsCode::MalformedDirectory,0,e.what()};
    }
}
std::pair<std::string,std::string> GameActorDictionary::ReadRecord(script::Vm& vm,uint32_t index) {
    Need(state_.imported&&!state_.closed,"Actor dictionary destination not live");
    Need(index<records_.size(),"Actor dictionary record index");
    const auto record=records_[index];
    Need(record.kind==script::Value::Kind::Reference,"Actor dictionary record reference");
    const auto written=vm.load_reference(vm.indexed_reference(record,1));
    const auto reading=vm.load_reference(vm.indexed_reference(record,2));
    Need(written.kind==script::Value::Kind::String&&reading.kind==script::Value::Kind::String,
         "Actor dictionary written/reading fields");
    ++state_.queries;return {written.cp932,reading.cp932};
}
bool GameActorDictionary::OwnsCell(const script::Value& ref) const noexcept {
    if(!state_.imported||state_.closed||ref.kind!=script::Value::Kind::Reference)return false;
    for(const auto& record:records_)
        if(ref.table==record.table&&!ref.frame_owner.owner_before(record.frame_owner)&&!record.frame_owner.owner_before(ref.frame_owner)&&ref.index>record.index&&uint32_t(ref.index)<=uint32_t(record.index)+19)return true;
    return false;
}
uint32_t GameActorDictionary::ImportedRecordsInRoot(const script::Vm& vm,const script::Value& root) const noexcept {
    try {
        const auto empty_owner=[](const auto& owner){const auto empty=decltype(owner.lock()){};return !owner.owner_before(empty)&&!empty.owner_before(owner);};
        const auto global=[&](const script::Value& ref){return ref.kind==script::Value::Kind::Reference&&ref.table==script::Table::Global
            &&empty_owner(ref.frame_owner)&&empty_owner(ref.context_owner)&&vm.stable_value_owner(ref);};
        // The same NativeGlobalGAME10Only boundary as the consuming capture.
        // Retired/borrowed Frame aliases are not a supported dictionary owner.
        if(!state_.imported||state_.closed||records_.empty()||!global(root))return 0;
        for(const auto& record:records_){if(!global(record))return 0;const auto h=vm.peek_reference(record);
            if(h.kind!=script::Value::Kind::ArrayHeader||h.word>>28!=9||!(h.word&0xFFF)||(h.word&0xFFF)>64)return 0;}
        const auto head=vm.peek_reference(root);const auto count=head.word&0xFFF;
        if(head.kind!=script::Value::Kind::ArrayHeader||head.word>>28!=9||count>600)return 0;
        std::vector<bool> seen(records_.size());uint32_t matched=0;
        for(uint32_t i=0;i<count;++i){
            if(uint64_t(root.index)+i+1>UINT16_MAX)return 0;
            auto cell=root;cell.index=uint16_t(root.index+i+1);const auto row=vm.peek_reference(cell);
            for(size_t k=0;k<records_.size();++k){const auto& record=records_[k];
                if(row.kind!=record.kind||row.word!=record.word||row.table!=record.table||row.index!=record.index
                   ||row.frame_owner.owner_before(record.frame_owner)||record.frame_owner.owner_before(row.frame_owner)
                   ||row.context_owner.owner_before(record.context_owner)||record.context_owner.owner_before(row.context_owner))continue;
                if(seen[k])return 0;seen[k]=true;++matched;break;
            }
        }
        return matched==records_.size()?matched:0;
    }catch(...){return 0;}
}
void GameActorDictionary::Capture(script::Vm& vm) {
    if(!state_.imported||state_.closed)return;
    std::vector<GameActorDictionaryRecordState> rows;
    for(uint32_t i=0;i<records_.size();++i){
        GameActorDictionaryRecordState row;row.index=i;const auto ref=records_[i];
        const auto at=[&](uint32_t n){auto value=ref;value.index=uint16_t(uint32_t(value.index)+1+n);return vm.peek_reference(value);};
        constexpr uint32_t fields[]{0,5,6,7,8,17};
        for(uint32_t n=0;n<6;++n)row.numeric[n]=at(fields[n]).word;
        row.fields14_15={{at(14).word,at(15).word}};
        row.written=at(1).cp932;row.reading=at(2).cp932;rows.push_back(std::move(row));
    }
    state_.current_records=std::move(rows);
}
void GameActorDictionary::Close() { image_.Reset();destination_={};records_.clear();state_.source_live=false;state_.imported=false;state_.closed=true; }
} // namespace dokodemo::recovered
