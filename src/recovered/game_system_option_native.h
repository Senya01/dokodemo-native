#pragma once
#include "game_main_menu_native.h"
#include "game_notebook_native.h"

namespace dokodemo::recovered {
struct GameOptionSettingsSnapshot {
    bool live=false;
    uint64_t owner=0,generation=0,writes=0;
    std::array<uint32_t,3> words{};
    std::array<uint8_t,32> protected_root{}; // full native root except the three typed words
};
struct GameOptionWriteReceipt {
    uint64_t gui_generation=0,input_sequence=0,input_epoch=0;
    uint32_t stage=0,field=0,call_pc=0,setter=0,value=0;
    GameOptionSettingsSnapshot before,after;
};
bool GameOptionSnapshotExact(const GameOptionSettingsSnapshot&,const GameOptionSettingsSnapshot&)noexcept;
bool GameOptionReceiptExact(const GameOptionWriteReceipt&,const GameOptionWriteReceipt&)noexcept;
bool GameOptionRightReceiptValid(const GameOptionWriteReceipt&)noexcept;
bool GameOptionBackupReceiptValid(const GameOptionWriteReceipt&)noexcept;
bool GameOptionYesReceiptValid(const GameOptionWriteReceipt&)noexcept;
enum class GameOptionRowCause:uint32_t {Down=1,Left=2,LeftCommon=3};
struct GameOptionRowWriteReceipt {
    GameOptionRowCause cause=GameOptionRowCause::Down;
    uint32_t selection_before=0,selection_after=1,row_count=3,level_count=5;
    std::array<uint32_t,3> local_before{},local_after{};
    GameOptionWriteReceipt write;
};
bool GameOptionRowReceiptValid(const GameOptionRowWriteReceipt&)noexcept;
bool GameOptionRowReceiptExact(const GameOptionRowWriteReceipt&,const GameOptionRowWriteReceipt&)noexcept;
struct GameOptionGainObservation {
    bool metadata_ready=false,source_id_known=false,source_dispatched=false;
    int32_t native_metadata_slot=-1;
    GameSourceSoundGainReceipt source;
};
class GameOptionSettings {
  public:
    virtual ~GameOptionSettings()=default;
    virtual GameOptionSettingsSnapshot OptionSettings()const=0;
    virtual GameFsStatus RestoreOptionSettings(const GameOptionSettingsSnapshot&,const std::array<uint32_t,3>&)=0;
    // Readonly providers retain their prior explicit direction refusal. Only
    // the current Content/actor owner may issue actual Right writer receipts.
    virtual bool OptionRightWritable()const{return false;}
    virtual bool OptionBackupWritable()const{return false;}
    virtual bool OptionRowWritable()const{return false;}
    virtual GameFsStatus WriteOptionRow(const GameOptionSettingsSnapshot&,GameOptionRowCause,uint32_t,uint32_t,GameOptionRowWriteReceipt&){return {GameFsCode::Unavailable,0,"Options selected row writer unavailable"};}
    virtual GameFsStatus ReadOptionFeedback(const GameOptionSettingsSnapshot&,GameSourceFeedbackRead&){return {GameFsCode::Unavailable,0,"Unsupported source SE5 bank handle read4AC84"};}
    virtual GameFsStatus ServiceOptionVoice(const GameOptionSettingsSnapshot&,GameSourceFeedbackRead&){return {GameFsCode::Unavailable,0,"NativeVoice service owner unavailable"};}
    virtual GameSourceSoundSnapshot OptionSoundOwner()const{return {};}
    virtual GameFsStatus WriteOptionRight(const GameOptionSettingsSnapshot&,uint32_t,uint32_t,GameOptionWriteReceipt&){return {GameFsCode::Unavailable,0,"Options Right writer owner unavailable"};}
    virtual GameFsStatus WriteOptionBackup(const GameOptionSettingsSnapshot&,uint32_t,uint32_t,GameOptionWriteReceipt&){return {GameFsCode::Unavailable,0,"Options backup writer owner unavailable"};}
    virtual GameFsStatus WriteOptionYes(const GameOptionSettingsSnapshot&,uint32_t,uint32_t,GameOptionWriteReceipt&){return {GameFsCode::Unavailable,0,"Options current Yes writer owner unavailable at3534C"};}
    virtual GameFsStatus QueryOptionGain(const GameOptionSettingsSnapshot&,int32_t,GameOptionGainObservation&){return {GameFsCode::Unavailable,0,"Unsupported Options source sound ID read4AFC8"};}
};
struct GameOwnedMessageSnapshot {
    bool live=false,template_owner=false,clone_owner=false,text_owner=false,registered=false,complete68=true,close69=false;
    uint32_t references=0,state64=0,mode98C=4,columns=9,rows=2,width=144,height=36;
    uint32_t text_calls=0,updates=0,draws=0,callbacks=0,emissions=0,glyphs=0,shown=0,total=0;
    uint32_t capacity=0,used=0,batch_stage=0,resets=0,flushes=0,rate_setting=2,rate_updates=0;
    int32_t delay70=0,idle74=0,flush_selector=0;
    float appearance=0,rate=0.75f,scale_x=0,scale_y=0;
    std::string text;
};
struct GameOwnedMessageShared;
// Distinct153D4 owner. Only the actual Options mode4/plain CP932 domain is
// admitted. The normal-mode message stack and control-code effects refuse.
class GameOwnedOptionMessage final:private GameUiCallbacks {
  public:
    GameOwnedOptionMessage()=default;
    GameOwnedOptionMessage(const GameOwnedOptionMessage&)=delete;
    GameOwnedOptionMessage& operator=(const GameOwnedOptionMessage&)=delete;
    GameFsStatus Initialize(GameResourceSource&,const GameFontResource&,uint32_t setting,
                            std::shared_ptr<GameOwnedMessageShared> shared={});
    GameFsStatus Text(const std::string&);
    GameFsStatus Tick(bool cross_held=false);
    GameFsStatus SetRate(uint32_t setting);
    GameFsStatus BindRateSource(GameOptionSettings&);
    GameFsStatus Draw(GameGraphics&);
    GameOwnedMessageSnapshot Snapshot()const;
    std::shared_ptr<GameOwnedMessageShared> Shared()const{return shared_;}
    void Close()noexcept;
    ~GameOwnedOptionMessage(){Close();}
  private:
    bool DrawUiCallback(uint32_t,uint32_t,const GameUiMatrix&,uint32_t,GameGraphics&,std::string&)override;
    GameOwnedMessageSnapshot state_;
    std::shared_ptr<GameOwnedMessageShared> shared_;
    std::vector<size_t> glyph_ends_;
    std::vector<uint8_t> unit_kinds_;
    size_t unit_cursor_=0;
    std::vector<std::string> lines_;
    GameOptionSettings* rate_source_=nullptr;
    uint64_t rate_owner_=0,rate_generation_=0;
};
struct GameSystemOptionSnapshot {
    bool requested=false,constructor_entered=false,template_owner=false,clone_owner=false,failed=false;
    // Constructor-idle never produces a completed Open/poll receipt.
    bool owner474=false,opened=false,poll_completed=false,retired=false,woken=false;
    uint64_t generation=0;
    uint32_t requests=0,initializer_attempts=0,resource=10,result=13,allocation_bytes=0x7C;
    uint32_t source_constructor=0x347C8,source_consumer=0,phase=0;
    GameMainMenuLease lease;
    std::array<uint32_t,3> passive_settings{};
    GameFsStatus failure;
    bool live=false,visible=false,request10=false,retirement_pending=false;
    uint64_t retired_generation=0,input_epoch=0,last_sequence=0;
    uint32_t state44=3,selection56=0,previous50=0,substate48=0,loader469=0;
    uint32_t opens=0,close_requests=0,close_completions=0,polls=0,retirement_requests=0,retirements=0,rejected=0,unsupported=0;
    uint32_t frame=0,clip=0,draw_frames=0,quads=0,manual_bars=0,source_ui_sound_intents=0,after_key_updates=0;
    GameNotebookLease child_lease;
    GameOptionSettingsSnapshot settings;
    std::array<uint32_t,3> current_words{}; //source64/68/6C; backups remain passive_settings
    uint32_t right_steps=0,right_writes=0,gain_queries=0,gain_value=0;
    uint32_t row_selections=0,row_left_steps=0,row_writes=0,row_feedback_attempts=0,marker_frame=1;
    std::array<uint32_t,3> row_before{};
    GameOwnedMessageSnapshot row_message_before; //passive actual Down entry, before selection mutation
    uint32_t row_draw_frames_before=0,row_left_after_key_before=0,row_left_draw_frames_before=0;
    std::array<GameOptionRowWriteReceipt,7> row_receipts{};
    GameSourceFeedbackRead row_feedback,row_voice_service;
    uint32_t voice_service_attempts=0;
    uint32_t right_message_updates_before=0,right_draw_frames_before=0;
    bool gain_returned=false,right_message_continued=false;
    GameOptionGainObservation gain;
    GameSourceSoundSnapshot sound_owner;
    std::array<GameOptionWriteReceipt,4> right_receipts{};
    std::array<uint32_t,3> bar_frames{}; // last successful authored Bind+Draw frames
    uint32_t restore_writes=0;
    std::array<GameOptionWriteReceipt,3> restore_receipts{};
    uint32_t yes_writes=0,yes_consumed=0,close_kind=0; //0none,1backupCancel,2currentYes
    std::array<GameOptionWriteReceipt,3> yes_receipts{};
    GameOwnedMessageSnapshot message;
    GameTrcPromptSnapshot prompt;
    uint64_t prompt_bound_owner=0,prompt_bound_generation=0;
    uint32_t input_boundaries=0,boundary_cause=0,prompt_opens=0,prompt_returns=0;
};
// Finite readonly first-row Options owner; other input routes explicitly refuse.
// Source1A3B0 cannot publish LoadMan474 or call34F1C until347C8 returns.
class GameSystemOptionFrontier final {
  public:
    GameFsStatus Request(GameMainMenuLease,std::array<uint32_t,3> passive_settings);
    GameFsStatus Advance(GameResourceSource&);
    GameFsStatus Initialize(GameResourceSource&,const GameFontResource&,GameOptionSettings&,GameTransition&,GameTrcPrompt* prompt=nullptr);
    GameFsStatus Tick(GameTransition&);
    GameFsStatus Draw(GameGraphics&,const GameFontResource&);
    bool Input(const GameInputFrame&);
    bool SetInputEpoch(uint64_t);
    void DisarmInput(){input_.BeginContext();if(prompt_&&prompt_->Snapshot().parent==state_.generation)prompt_->Disarm();}
    bool Modal()const{return state_.live&&state_.phase>=2&&state_.phase<=6;}
    bool ClosedAndRetired()const{return state_.phase==7&&state_.retired_generation==state_.generation&&state_.generation;}
    GameSystemOptionSnapshot Snapshot()const;
    void SetChildLease(GameNotebookLease l){state_.child_lease=l;state_.input_epoch=l.input_epoch;}
    void Abort()noexcept;
  private:
    friend class GameContentBoot;
    bool PublishFeedback(const GameSourceFeedbackRead&);
    bool PublishVoiceService(const GameSourceFeedbackRead&);
    GameFsStatus Refuse(GameFsStatus);
    GameSystemOptionSnapshot state_;
    std::shared_ptr<GameUiResource> resource_;
    GameUiInstance clone_;
    GameFsStatus PrepareTemplate(GameResourceSource&);
    GameOptionSettings* settings_=nullptr;
    const GameFontResource* font_=nullptr;
    GameMenuInput input_;
    GameOwnedOptionMessage message_;
    GameTrcPrompt* prompt_=nullptr;
    bool PromptOwnerLive()const;
};
}
