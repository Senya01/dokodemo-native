#pragma once
#include "../src/recovered/game_system_option_native.h"
#include <stdexcept>
// Include game_options_confirm_regressions.h first. Only its public fixture
// prefix (through PromptRig, line157) is reused; no old test entry is called.
// SOURCE authored; author execution count0. Synthetic transport/ink does not
// issue private actor authority, paused18, completion, or Bootstrap provenance.
namespace dokodemo::options_yes_tests {
using namespace recovered;
namespace f=options_confirm_tests;
using U=uint32_t;using Q=uint64_t;
inline void C(bool v,const char* id){if(!v)throw std::runtime_error(std::string("options-yes ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-yes ")+id+": "+s.detail);}
inline bool Eq(const GameFsStatus& a,const GameFsStatus& b){return a.code==b.code&&a.native_error==b.native_error&&a.detail==b.detail;}

// This adapter deliberately has no WriteOptionYes override. Its Right/backup
// transport is synthetic. Its gain query calls the genuine public sound owner.
struct WithoutYes:GameOptionSettings {
    f::Settings base;
    GameOptionSettingsSnapshot OptionSettings()const override{return base.OptionSettings();}
    GameSourceSoundSnapshot OptionSoundOwner()const override{return base.OptionSoundOwner();}
    bool OptionRightWritable()const override{return base.OptionRightWritable();}
    bool OptionBackupWritable()const override{return base.OptionBackupWritable();}
    GameFsStatus RestoreOptionSettings(const GameOptionSettingsSnapshot& e,const std::array<U,3>& w)override{return base.RestoreOptionSettings(e,w);}
    GameFsStatus WriteOptionRight(const GameOptionSettingsSnapshot& e,U s,U v,GameOptionWriteReceipt& r)override{return base.WriteOptionRight(e,s,v,r);}
    GameFsStatus WriteOptionBackup(const GameOptionSettingsSnapshot& e,U s,U v,GameOptionWriteReceipt& r)override{return base.WriteOptionBackup(e,s,v,r);}
    GameFsStatus QueryOptionGain(const GameOptionSettingsSnapshot& e,int32_t v,GameOptionGainObservation& r)override{return base.QueryOptionGain(e,v,r);}
};
struct Settings final:WithoutYes {
    std::vector<GameOptionWriteReceipt> yes;
    U attempts=0,fail_stage=99,corrupt_stage=99,corruption=0;
    GameFsStatus injected{GameFsCode::IoFailure,0x12345678,"Yes transport failure"};
    GameFsStatus WriteOptionYes(const GameOptionSettingsSnapshot& expected,U stage,U value,GameOptionWriteReceipt& out)override{
        ++attempts;
        if(!GameOptionSnapshotExact(expected,base.state)||stage>=3||stage!=yes.size()||base.state.writes==UINT64_MAX)
            return {GameFsCode::Unavailable,0,"Yes fixture order/owner/overflow"};
        if(stage==fail_stage)return injected;
        GameOptionWriteReceipt r;r.gui_generation=base.gui;r.input_sequence=base.sequence;r.input_epoch=base.epoch;
        r.stage=r.field=stage;r.call_pc=0x3534C+stage*8;r.setter=0x24B1C+stage*0x18;r.value=value;
        r.before=base.state;base.state.words[stage]=value;++base.state.writes;r.after=base.state;
        yes.push_back(r);base.events.push_back(r.call_pc);
        if(stage==corrupt_stage)switch(corruption){
            case 0:++r.stage;break;case 1:r.field=(stage+1)%3;break;
            case 2:r.call_pc=0x3526C+stage*8;break;case 3:r.setter+=4;break;
            case 4:++r.gui_generation;break;case 5:++r.input_sequence;break;case 6:++r.input_epoch;break;
            case 7:r.value^=0x10000;break;case 8:++r.before.owner;break;case 9:++r.before.generation;break;
            case 10:++r.before.writes;break;case 11:r.after.words[(stage+1)%3]^=0x80000000;break;
            case 12:++base.state.writes;break;case 13:r.after.writes=r.before.writes;break;
        }
        out=r;return {};
    }
};
template<class Provider=Settings> struct Rig {
    f::PromptRig trc;Provider settings;GameSystemOptionFrontier option;GameTransition fade;GameGraphics graphics;Q sequence=0;
    Rig(){
        C(fade.Initialize({{0,0}}),"fixture fade");fade.SetDuration(4);
        Ok(option.Request(f::Lease(),settings.base.state.words),"fixture request");
        Ok(option.Initialize(trc.source,trc.font,settings,fade,&trc.prompt),"fixture initialize");
        option.SetChildLease(f::Child());settings.base.gui=option.Snapshot().generation;
        for(U i=0;i<40&&option.Snapshot().phase!=3;++i)Ok(Step(),"fixture expansion");
        C(option.Snapshot().phase==3&&option.Snapshot().state44==1,"fixture active finite");
        C(graphics.Initialize({}),"fixture graphics");trc.source.blocked=true;
    }
    ~Rig(){option.Abort();}
    GameFsStatus Step(){fade.Update();return option.Tick(fade);}
    GameInputFrame Frame(U buttons){const auto s=option.Snapshot();GameInputFrame p;
        p.buttons=buttons;p.focused=true;p.sequence=++sequence;p.main_menu_epoch=s.input_epoch;
        p.message_owner_generation=s.child_lease.script_generation;p.message_context=s.child_lease.context;
        p.returned_owner=s.child_lease.available_owner;p.returned_generation=s.child_lease.available_generation;return p;}
    bool Input(GameInputFrame p){settings.base.sequence=p.sequence;settings.base.epoch=p.main_menu_epoch;return option.Input(p);}
    void Send(U buttons){C(Input(Frame(buttons)),"fixture current input");Ok(Step(),"fixture input tick");}
    void Right(){Send(0);Send(recovered::Right);const auto s=option.Snapshot();
        C(s.right_steps==1&&s.right_writes==4&&s.gain_queries==1&&s.gain_returned&&s.right_message_continued,"R01");
        C(s.current_words==std::array<U,3>{{3,3,3}}&&s.passive_settings==std::array<U,3>{{2,3,3}}&&s.settings.writes==4,"R02");
        C(GameSourceSoundGainValid(s.gain.source)&&s.gain.source.returned&&s.gain.source.value==105
          &&GameSourceSoundSnapshotExact(s.sound_owner,settings.base.sound.Snapshot()),"R03 genuine Gain4AFBC");
    }
    void Open(){Send(0);Send(Circle);const auto s=option.Snapshot();
        C(s.substate48==3&&s.prompt.state864==-1&&s.prompt.result86C==0&&s.prompt_opens==1,"C01");
        C(s.input_boundaries==2&&s.boundary_cause==2&&s.right_writes==4&&!s.restore_writes&&!s.yes_writes,"C02");
    }
    void Active(){for(U i=0;i<20&&trc.prompt.Snapshot().state864!=1;++i)Ok(Step(),"nested expansion");
        const auto s=option.Snapshot();C(s.prompt.state864==1&&s.prompt.active&&s.prompt.selection==0,"B01");
        C(s.input_boundaries==3&&s.boundary_cause==3&&!s.yes_writes&&s.settings.writes==4,"B02");
    }
    void PressYes(){Send(0);Send(Circle);const auto s=option.Snapshot();
        C(s.phase==3&&s.substate48==3&&s.prompt.state864==2&&s.prompt.result86C==1&&s.prompt.active,"Y01");
        C(s.prompt.yes_choices==1&&!s.prompt.yes_refusals&&!s.prompt.returns&&!s.prompt.completed_invocation,"Y02");
        C(!s.yes_writes&&!s.yes_consumed&&!s.close_requests&&!s.restore_writes&&s.settings.writes==4,"Y03");
        C(s.prompt.result_sequence==s.last_sequence&&s.prompt.result_epoch==s.input_epoch,"Y04");
    }
    void BeforeIdle(){const U returns=trc.prompt.Snapshot().returns;for(U i=0;i<20&&trc.prompt.Snapshot().progress>.1001f;++i){
        Ok(Step(),"nested contraction prefix");const auto s=option.Snapshot();
        C(s.prompt.state864==2&&s.phase==3&&!s.yes_writes&&!s.yes_consumed&&s.prompt.returns==returns,"Y05");}
        C(trc.prompt.Snapshot().state864==2&&trc.prompt.Snapshot().progress>0&&trc.prompt.Snapshot().progress<=.1001f,"Y06 finite final contraction");
    }
    void Draw(){C(graphics.Clear(0),"fixture clear");Ok(option.Draw(graphics,trc.font),"component draw");}
    void PollAndDrain(){const Q generation=option.Snapshot().generation;const U writes=U(settings.base.state.writes);
        for(U i=0;i<40&&option.Snapshot().phase==4;++i)Ok(Step(),"close contraction");
        const auto p=option.Snapshot();
        C(p.phase==6&&p.state44==3&&p.poll_completed&&p.polls==1&&p.close_completions==1,"W01 poll6");
        C(p.live&&p.owner474&&p.template_owner&&p.clone_owner&&p.message.live&&p.message.text_owner
          &&p.retirement_pending&&!p.retired&&!p.retired_generation&&!p.retirements,"W02 live owners at poll6");
        C(p.generation==generation&&!p.visible&&!p.loader469&&!p.request10&&!p.woken&&!option.ClosedAndRetired(),"W03");
        C(p.prompt.live&&p.prompt.resource_live&&p.prompt.font_live&&p.prompt.completed_invocation==p.prompt.invocation,"W04");
        Ok(option.Tick(fade),"distinct following drain tick");const auto d=option.Snapshot();
        C(d.phase==7&&d.retired&&d.retired_generation==generation&&d.retirements==1&&option.ClosedAndRetired(),"W05 drain7");
        C(!d.live&&!d.owner474&&!d.template_owner&&!d.clone_owner&&!d.message.live&&!d.message.text_owner
          &&!d.retirement_pending&&!d.woken&&d.polls==1&&settings.base.state.writes==writes,"W06 no wake authority");
        Ok(option.Tick(fade),"drain no replay");C(option.Snapshot().retirements==1&&settings.base.state.writes==writes,"W07");
    }
};

// Family1: actual producer only. No Options consumer or private publication.
inline void ProducerBoundaries(){
    {f::PromptRig r;r.Open();r.Active();const auto cold=r.prompt.Snapshot();
        r.Send(Circle);r.Send(Circle);C(r.prompt.Snapshot().state864==1&&!r.prompt.Snapshot().result86C,"P01 held at activation");
        r.Send(0);r.Send(Circle);auto s=r.prompt.Snapshot();
        C(s.state864==2&&s.result86C==1&&s.yes_choices==1&&!s.no_choices&&!s.yes_refusals&&!s.returns&&!s.completed_invocation,"P02 raw1 on press");
        C(s.result_sequence==r.seq&&s.result_epoch==r.epoch&&s.result_update==s.updates&&s.publication==1,"P03");
        C(!r.prompt.Input(r.Frame(Circle)),"P04 closing rejects input");
        for(U i=0;i<20&&r.prompt.Snapshot().state864!=3;++i)Ok(r.prompt.Tick(),"producer contraction");s=r.prompt.Snapshot();
        C(s.state864==3&&!s.active&&s.result86C==1&&s.returns==1&&s.completed_invocation==s.invocation
          &&s.owner==cold.owner&&s.generation==cold.generation,"P05 natural idle receipt");
        const U publication=s.publication;Ok(r.prompt.Tick(),"idle no replay");C(r.prompt.Snapshot().publication==publication,"P06");
        Ok(r.prompt.Open(73,r.epoch),"producer reentry");r.Active();r.Send(Circle);
        C(r.prompt.Snapshot().invocation==2&&!r.prompt.Snapshot().result86C&&r.prompt.Snapshot().state864==1,"P07 boundary held");
        r.Send(0);r.Send(Circle);C(r.prompt.Snapshot().result86C==1&&r.prompt.Snapshot().publication==2,"P08 fresh second Yes");
    }
    {f::PromptRig r;r.Open();r.Active();r.Send(0);r.Send(Down|Circle|Cross);
        C(r.prompt.Snapshot().selection==1&&!r.prompt.Snapshot().result86C&&!r.prompt.Snapshot().disabled_cross,"P09 Down priority");
        r.Send(0);r.Send(Up|Circle);C(!r.prompt.Snapshot().selection&&!r.prompt.Snapshot().result86C,"P10 Up priority");
        r.Send(0);r.Send(Circle|Cross);C(r.prompt.Snapshot().result86C==1&&!r.prompt.Snapshot().disabled_cross,"P11 Circle priority");
    }
    for(U ignored:{Right,Left})for(U held=0;held<2;++held){f::PromptRig r;r.Open();r.Active();
        if(held){r.Send(ignored|Circle);C(!r.prompt.Snapshot().result86C,"P18 held Circle");r.Send(ignored);}else r.Send(0);
        r.Send(ignored|Circle);C(r.prompt.Snapshot().result86C==1&&r.prompt.Snapshot().state864==2,"P19 ignored direction/per-Circle release");
    }
    {f::PromptRig r;r.Open();r.Active();r.Send(0);r.Send(Down);r.Send(0);r.Send(Up);r.Send(Up|Circle);
        C(r.prompt.Snapshot().result86C==1&&!r.prompt.Snapshot().selection,"P20 held Up Repeat0 permits Circle");
    }
    for(U cause=0;cause<3;++cause){f::PromptRig r;r.Open();r.Active();r.Send(0);auto bad=r.Frame(0);
        if(cause==0)bad.focused=false;else if(cause==1)bad.overflow_resync=true;else ++bad.main_menu_epoch;
        C(!r.prompt.Input(bad),"P12 rejected packet");Ok(r.prompt.Tick(),"rejected packet tick");
        if(cause<2){r.Send(Circle);C(!r.prompt.Snapshot().result86C,"P13 focus overflow disarm");}
        else{auto stale=r.Frame(Circle);stale.sequence=0;C(!r.prompt.Input(stale),"P14 stale sequence");}
        r.Send(0);r.Send(Circle);C(r.prompt.Snapshot().result86C==1&&r.prompt.Snapshot().state864==2,"P15 fresh recovery");
    }
    {f::PromptRig r;r.Open();r.Active();r.Send(0);r.Send(Cross);
        C(r.prompt.Snapshot().state864==1&&!r.prompt.Snapshot().result86C&&r.prompt.Snapshot().disabled_cross==1,"P16 disabled Cross");
        r.Send(0);r.Send(Down);r.Send(0);r.Send(Circle);
        C(r.prompt.Snapshot().result86C==2&&r.prompt.Snapshot().state864==2&&r.prompt.Snapshot().no_choices==1&&!r.prompt.Snapshot().yes_choices,"P17 raw2 distinct");
    }
}

inline GameOptionWriteReceipt Receipt(U stage,U value,Q writes=9){GameOptionWriteReceipt r;
    r.gui_generation=17;r.input_sequence=18;r.input_epoch=19;r.stage=r.field=stage;r.call_pc=0x3534C+8*stage;r.setter=0x24B1C+0x18*stage;r.value=value;
    r.before={true,101,7,writes,{{0x12345678,0x80000001,0xFFFFFFFF}}};r.after=r.before;r.after.words[stage]=value;++r.after.writes;return r;
}
// Family2: pure validators examine complete32-bit words and exact snapshots.
// A structurally valid public receipt is not a private actor-issued receipt.
inline void ReceiptDomains(){
    for(U stage=0;stage<3;++stage)for(U value:{0u,3u,0x80000000u,UINT32_MAX}){
        auto r=Receipt(stage,value);C(GameOptionYesReceiptValid(r),"V01 full32 Yes");
        C(!GameOptionBackupReceiptValid(r)&&!GameOptionRightReceiptValid(r),"V02 cross domain");
        auto equal=r;equal.before.words[stage]=value;equal.after.words[stage]=value;
        C(GameOptionYesReceiptValid(equal)&&equal.after.writes==equal.before.writes+1,"V03 equal still stores");
        auto backup=r;backup.call_pc=0x3526C+stage*8;
        C(GameOptionBackupReceiptValid(backup)&&!GameOptionYesReceiptValid(backup),"V04 backup partition");
        auto near=r;near.before.writes=UINT64_MAX-1;near.after.writes=UINT64_MAX;
        C(GameOptionYesReceiptValid(near),"V05 last representable write");
        near.before.writes=UINT64_MAX;near.after.writes=0;C(!GameOptionYesReceiptValid(near),"V06 no write wrap");
        for(U bad=0;bad<27;++bad){auto x=r;switch(bad){
            case 0:x.gui_generation=0;break;case 1:x.input_sequence=0;break;case 2:x.input_epoch=0;break;
            case 3:x.stage=3;break;case 4:x.stage=(stage+1)%3;break;case 5:x.field=(stage+1)%3;break;
            case 6:x.call_pc+=4;break;case 7:x.setter+=4;break;case 8:x.value^=0x10000;break;
            case 9:x.before.live=false;break;case 10:x.before.owner=0;break;case 11:x.before.generation=0;break;
            case 12:x.after.live=false;break;case 13:++x.after.owner;break;case 14:++x.after.generation;break;
            case 15:x.after.writes=x.before.writes;break;case 16:x.after.writes+=1;break;
            case 17:x.after.words[stage]^=0x10000;break;case 18:x.after.words[(stage+1)%3]^=0x80000000;break;
            case 19:x.before.words[(stage+2)%3]^=0x10000;break;case 20:x.before.writes=UINT64_MAX;break;
            case 21:x.call_pc=0x3526C+stage*8;break;case 22:x.call_pc=0x34CCC+stage*8;break;
            case 23:x.field=UINT32_MAX;break;case 24:x.after.words[stage]=value^0x80000000;break;
            case 25:x.after.generation=0;break;case 26:x.after.owner=0;break;
        }C(!GameOptionYesReceiptValid(x)&&!GameOptionReceiptExact(x,r),"V07 mutation rejected/exact differs");}
        C(GameOptionReceiptExact(r,r)&&GameOptionSnapshotExact(r.before,r.before),"V08 reflexive exact");
        auto changed=r;++changed.gui_generation;C(GameOptionYesReceiptValid(changed)&&!GameOptionReceiptExact(r,changed),"V09 validator has no issuance oracle");
    }
    static constexpr U fields[]={1,0,1,2},calls[]={0x351CC,0x34CCC,0x34CD4,0x34CDC},setters[]={0x24B34,0x24B1C,0x24B34,0x24B4C};
    for(U stage=0;stage<4;++stage){auto r=Receipt(0,3);r.stage=stage;r.field=fields[stage];r.call_pc=calls[stage];r.setter=setters[stage];
        r.after=r.before;r.after.words[r.field]=3;++r.after.writes;
        C(GameOptionRightReceiptValid(r)&&!GameOptionYesReceiptValid(r)&&!GameOptionBackupReceiptValid(r),"V10 Right partition");}
}

// Family3: real public frontier/borrowed prompt, natural complete+incomplete68
// paths. Finite offset search aligns the genuine message timing; no state edits.
inline void CurrentChain(bool complete68){bool found=false;
    for(U delay=0;delay<192&&!found;++delay){Rig<> r;r.Right();r.Draw();
        C(r.option.Snapshot().bar_frames==std::array<U,3>{{9,4,4}},"T01 current bar frames");
        r.Open();r.Active();for(U i=0;i<delay;++i)Ok(r.Step(),"helper alignment wait");r.PressYes();r.BeforeIdle();
        const auto before=r.option.Snapshot();if(before.message.complete68!=complete68)continue;found=true;
        const auto root=before.settings;const U sounds=before.source_ui_sound_intents,io=r.trc.source.opens;
        Ok(r.Step(),"natural Yes current stores");const auto s=r.option.Snapshot();
        C(s.prompt.state864==3&&!s.prompt.active&&s.prompt.result86C==1&&s.prompt.returns==1
          &&s.prompt.completed_invocation==s.prompt.invocation,"T02 natural completed invocation");
        C(s.phase==4&&s.state44==2&&s.substate48==3&&s.close_kind==2&&s.yes_consumed==1&&s.yes_writes==3&&s.source_consumer==0x35364,"T03 Yes close keeps48=3");
        C(!s.restore_writes&&!s.prompt_returns&&s.close_requests==1&&s.right_writes==4&&s.settings.writes==7,"T04 R4 Yes3 W7");
        C(s.input_boundaries==3&&s.boundary_cause==3&&!s.woken&&!s.poll_completed&&!s.retirements,"T05 no return2/private wake");
        C(s.current_words==std::array<U,3>{{3,3,3}}&&s.passive_settings==std::array<U,3>{{2,3,3}}
          &&s.settings.words==s.current_words&&GameOptionSnapshotExact(s.settings,r.settings.OptionSettings()),"T06 current not backup");
        C(s.after_key_updates==before.after_key_updates+1&&s.message.updates==before.message.updates+1
          &&s.message.text_calls==before.message.text_calls+(complete68?1u:0u),"T07 afterKey+actual helper gate");
        C(s.source_ui_sound_intents==sounds+1&&r.settings.base.gain_calls==1&&s.gain_queries==1
          &&r.settings.base.restore_calls==0&&r.trc.source.opens==io,"T08 Close only no extra gain IO");
        auto previous=root;
        for(U stage=0;stage<3;++stage){const auto& receipt=s.yes_receipts[stage];
            C(GameOptionYesReceiptValid(receipt)&&receipt.stage==stage&&receipt.field==stage
              &&receipt.call_pc==0x3534C+stage*8&&receipt.setter==0x24B1C+stage*0x18,"T09 typed stages");
            C(receipt.gui_generation==s.generation&&receipt.input_sequence==s.prompt.result_sequence
              &&receipt.input_epoch==s.prompt.result_epoch&&receipt.value==3,"T10 current natural input binding");
            C(GameOptionSnapshotExact(receipt.before,previous)&&receipt.before.words==receipt.after.words
              &&receipt.after.writes==receipt.before.writes+1&&GameOptionReceiptExact(receipt,r.settings.yes.at(stage)),"T11 three equal stores exact chain");
            previous=receipt.after;
        }
        C(r.settings.attempts==3&&r.settings.base.events==std::vector<U>({0x351CC,0x34CCC,0x34CD4,0x34CDC,0x4AFBC,0x3534C,0x35354,0x3535C}),"T12 writer+genuine gain event order");
        auto copy=s;copy.yes_receipts[0].after.words[0]=2;copy.prompt.completed_invocation=0;
        C(GameOptionReceiptExact(r.option.Snapshot().yes_receipts[0],s.yes_receipts[0])
          &&r.trc.prompt.Snapshot().completed_invocation==s.prompt.invocation,"T13 snapshots cannot issue or mutate authority");
        r.Draw();C(r.option.Snapshot().bar_frames==std::array<U,3>{{9,4,4}},"T14 close draws current bars");
        C(!r.Input(r.Frame(Circle))&&!r.option.SetInputEpoch(s.input_epoch+1),"T15 close rejects input/epoch");r.PollAndDrain();
    }C(found,"T16 finite genuine helper alignment found");
}

// Family4: transport failures keep the first status exactly, including empty
// detail. Successful prefix is visible and Abort is teardown, not restoration.
inline void FailurePrefixes(){
    for(U stage=0;stage<3;++stage)for(U empty=0;empty<2;++empty){Rig<> r;r.Right();r.Open();r.Active();r.PressYes();r.BeforeIdle();
        const auto before=r.option.Snapshot();r.settings.fail_stage=stage;
        r.settings.injected={stage==1?GameFsCode::Bounds:GameFsCode::IoFailure,0xF0000100+stage,empty?"":"first Yes failure"};
        const auto expected=r.settings.injected;const auto status=r.Step();const auto s=r.option.Snapshot();
        C(!status&&Eq(status,expected)&&s.failed&&Eq(s.failure,expected)&&s.source_consumer==0x3534C+stage*8,"F01 exact first status/PC");
        C(s.yes_writes==stage&&r.settings.yes.size()==stage&&r.settings.attempts==stage+1
          &&s.settings.writes==4+stage&&GameOptionSnapshotExact(s.settings,r.settings.OptionSettings()),"F02 successful prefix retained");
        C(s.phase==3&&s.substate48==3&&!s.yes_consumed&&!s.close_kind&&!s.close_requests&&!s.restore_writes
          &&!s.polls&&!s.retirements&&!s.woken&&s.prompt.state864==3&&s.prompt.completed_invocation==s.prompt.invocation,"F03 no false close/authority");
        C(s.after_key_updates==before.after_key_updates&&s.message.updates==before.message.updates,"F04 failure stops common tail");
        for(U i=0;i<stage;++i)C(GameOptionYesReceiptValid(s.yes_receipts[i])&&GameOptionReceiptExact(s.yes_receipts[i],r.settings.yes[i]),"F05 retained typed prefix");
        const U attempts=r.settings.attempts;const auto retained=r.settings.OptionSettings();r.settings.injected={GameFsCode::NotFound,9,"replacement"};
        C(Eq(r.Step(),expected)&&Eq(r.option.Draw(r.graphics,r.trc.font),expected)&&!r.Input(r.Frame(Cross)),"F06 first failure sticky");
        C(r.settings.attempts==attempts&&GameOptionSnapshotExact(retained,r.settings.OptionSettings()),"F07 no replay");
        r.option.Abort();C(GameOptionSnapshotExact(retained,r.settings.OptionSettings())&&r.settings.base.backup.empty()
          &&r.settings.base.restore_calls==0&&!r.option.Snapshot().woken&&!r.option.Snapshot().retired,"F08 Abort no restoration/completion");
    }
    for(U stage=0;stage<3;++stage)for(U corruption=0;corruption<14;++corruption){Rig<> r;r.Right();r.Open();r.Active();r.PressYes();r.BeforeIdle();
        r.settings.corrupt_stage=stage;r.settings.corruption=corruption;const auto status=r.Step();const auto s=r.option.Snapshot();
        C(!status&&s.failed&&s.source_consumer==0x3534C+stage*8&&s.yes_writes==stage,"F09 consumer rejects receipt/readback corruption");
        C(r.settings.attempts==stage+1&&r.settings.yes.size()==stage+1&&!s.yes_consumed&&!s.close_requests
          &&!s.restore_writes&&!s.polls&&!s.retirements&&!s.woken,"F10 malformed mutation gets no credit");
        const auto provider=r.settings.OptionSettings();const U attempts=r.settings.attempts;
        C(Eq(r.Step(),status)&&r.settings.attempts==attempts,"F11 malformed no replay");r.option.Abort();
        C(GameOptionSnapshotExact(provider,r.settings.OptionSettings())&&r.settings.base.restore_calls==0,"F12 malformed Abort preserves provider facts");
    }
}

// Family5: dependencies, packet identities, No/Cancel, and unsupported domains.
inline void DependenciesAndPartitions(){
    {Rig<WithoutYes> r;r.Right();r.Open();r.Active();r.PressYes();r.BeforeIdle();const auto status=r.Step();const auto s=r.option.Snapshot();
        C(!status&&status.code==GameFsCode::Unavailable&&status.native_error==0
          &&status.detail=="Options current Yes writer owner unavailable at3534C"&&s.source_consumer==0x3534C,"N01 missing writer exact3534C");
        C(s.prompt.state864==3&&s.prompt.result86C==1&&s.prompt.completed_invocation==s.prompt.invocation
          &&s.gain_returned&&r.settings.base.gain_calls==1&&s.settings.writes==4&&!s.yes_writes&&!s.close_requests&&!s.restore_writes,"N02 genuine idle before refusal");
    }
    for(U dependency=0;dependency<8;++dependency){Rig<> r;r.Right();r.Open();r.Active();r.PressYes();
        switch(dependency){case 0:++r.settings.base.state.owner;break;case 1:++r.settings.base.state.generation;break;
            case 2:r.settings.base.state.words[1]^=0x80000000;break;case 3:r.trc.font.Close();break;
            case 4:r.settings.base.bank->live=false;break;case 5:r.trc.prompt.Close();break;
            case 6:++r.trc.source.backing->generation;break;case 7:r.fade.Retire();break;}
        const auto status=r.Step();const auto s=r.option.Snapshot();
        C(!status&&s.failed&&!s.yes_writes&&!s.yes_consumed&&!s.close_requests&&!s.polls&&!s.retirements&&!s.woken,"N03 retired dependency blocks stores");
        C(r.settings.attempts==0&&r.settings.base.gain_calls==1&&Eq(r.Step(),status),"N04 dependency failure sticky");
    }
    for(U identity=0;identity<7;++identity){Rig<> r;r.Right();r.Open();r.Active();r.Send(0);auto bad=r.Frame(Circle);
        switch(identity){case 0:++bad.message_owner_generation;break;case 1:++bad.message_context;break;
            case 2:++bad.returned_owner;break;case 3:++bad.returned_generation;break;case 4:++bad.main_menu_epoch;break;
            case 5:bad.sequence=r.option.Snapshot().last_sequence;break;case 6:bad.overflow_resync=true;break;}
        C(!r.Input(bad),"N05 wrong packet owner/gen/order");Ok(r.Step(),"rejected nested frame");
        C(!r.option.Snapshot().prompt.result86C&&!r.option.Snapshot().yes_writes,"N06 no raw result from invalid identity");
        r.Send(0);r.Send(Circle);C(r.option.Snapshot().prompt.result86C==1&&!r.option.Snapshot().yes_writes,"N07 fresh valid producer after rejection");
    }
    {Rig<> r;r.Right();r.Open();r.Active();r.Send(0);r.Send(Down);r.Send(0);r.Send(Circle);
        C(r.option.Snapshot().prompt.result86C==2&&!r.option.Snapshot().yes_writes,"N08 actual No producer");
        for(U i=0;i<20&&r.option.Snapshot().substate48;++i)Ok(r.Step(),"No natural return");const auto no=r.option.Snapshot();
        C(no.phase==3&&!no.substate48&&no.prompt_returns==1&&no.input_boundaries==4&&!no.yes_writes&&!no.restore_writes&&!no.close_kind,"N09 No resumes same Options");
        r.Send(Circle);C(!r.option.Snapshot().substate48,"N10 held Circle after No boundary");
        r.Send(0);const auto before=r.option.Snapshot();r.Send(Cross);const auto cancel=r.option.Snapshot();
        C(cancel.phase==4&&cancel.close_kind==1&&!cancel.substate48&&cancel.restore_writes==3&&!cancel.yes_writes&&!cancel.yes_consumed,"N11 backupCancel distinct");
        C(cancel.settings.words==std::array<U,3>{{2,3,3}}&&cancel.settings.writes==7
          &&cancel.source_ui_sound_intents==before.source_ui_sound_intents+2&&r.settings.yes.empty(),"N12 Cancel backups and sound partition");
        C(cancel.after_key_updates==before.after_key_updates+1&&cancel.message.updates==before.message.updates+1,"N13 Cancel common tail");
        r.PollAndDrain();
    }
    {Rig<> r;r.Right();r.Open();r.Active();r.Send(0);r.Send(Down);r.Send(0);r.Send(Circle);
        for(U i=0;i<20&&r.option.Snapshot().substate48;++i)Ok(r.Step(),"No reentry return");
        r.Send(0);r.Send(Circle);const auto second=r.option.Snapshot();
        C(second.prompt.invocation==2&&second.prompt_opens==2&&second.prompt.result86C==0&&second.prompt.state864==-1,"N14 new invocation after No");
        for(U i=0;i<20&&r.trc.prompt.Snapshot().state864!=1;++i)Ok(r.Step(),"second natural expansion");
        r.Send(Circle);C(!r.option.Snapshot().prompt.result86C,"N15 second context release required");
        r.Send(0);r.Send(Circle);r.BeforeIdle();Ok(r.Step(),"second invocation Yes");
        C(r.option.Snapshot().yes_consumed==1&&r.option.Snapshot().prompt.result86C==1
          &&r.option.Snapshot().prompt.completed_invocation==2&&r.option.Snapshot().close_kind==2,"N16 Yes after No keeps invocation binding");
    }
    {Rig<> r;const auto before=r.option.Snapshot();C(!r.option.Request(f::Lease(),{{2,3,3}}),"N17 live re-request rejected");
        C(r.option.Snapshot().generation==before.generation&&r.option.Snapshot().opens==1,"N18 no generation replacement");
        r.Send(0);auto input=r.Frame(Down|Circle);C(r.Input(input),"domain priority packet");const auto status=r.Step();
        C(!status&&r.option.Snapshot().source_consumer==0x350B0&&!r.option.Snapshot().prompt_opens&&!r.option.Snapshot().yes_writes,"N19 unsupported direction precedes Circle");
    }
}
} // namespace dokodemo::options_yes_tests
inline void GameOptionsYesRegressionChecks(){
    using namespace dokodemo::options_yes_tests;
    ProducerBoundaries();ReceiptDomains();CurrentChain(false);CurrentChain(true);FailurePrefixes();DependenciesAndPartitions();
}
