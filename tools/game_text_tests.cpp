#include "recovered/game_profile_native.h"
#include "recovered/game_text_native.h"
#include "game_window_input.h"
#include <algorithm>
#ifdef _WIN32
#include "game_text_win32.h"
#include "game_text_transaction_tests.h"
#endif
#include <iostream>
#include <stdexcept>
using namespace dokodemo::recovered;
namespace {
void Check(bool value, const char *what) {
    if (!value)
        throw std::runtime_error(what);
}
void WindowButtonsTests() {
    native_port::GameWindowButtons keys;
    constexpr uint32_t circle=1,down=2;
    Check(keys.Update(circle,true,false,true,false)&&keys.Mask()==circle,"focused fresh key");
    keys.FocusLost();
    Check(!keys.Update(circle,true,false,false,false)&&keys.Mask()==0,"queued down after focus loss refused");
    Check(!keys.Update(circle,true,true,true,false)&&keys.Mask()==0,"held refocus repeat does not rearm");
    Check(keys.Update(down,true,false,true,false)&&keys.Mask()==down,"fresh Down cannot revive old Circle");
    Check(keys.Update(circle,true,false,true,false)&&keys.Mask()==(down|circle),"complete aggregate mask");
    Check(keys.Update(circle,false,false,true,true)&&keys.Mask()==down,"editor still releases old key");
    Check(!keys.Update(circle,true,false,true,true)&&keys.Mask()==down,"editor key does not enter game");
    Check(keys.Update(0,false,false,false,false)&&keys.Mask()==0,"unfocused event clears all keys");
}
void ProfileTests() {
    GameProfile p;
    std::string error;
    const auto result = [&](GameProfileField field, const char *value) {
        p.Begin(field);
        const std::string s = value;
        Check(p.Result(&s, 2026, error), error.c_str());
    };
    p.Nickname("\x83\x67\x83\x8D");
    Check(p.Snapshot().present == 1 && p.Next() == GameProfileField::Player,
          "count1 player,not fabricated default");
    p.Begin(GameProfileField::Player);
    Check(p.Result(nullptr, 2026, error) && p.Snapshot().player.empty(),
          "cancel empty player restores backup");
    result(GameProfileField::Player, "Sen");
    Check(p.Snapshot().present == 2 && p.Next() == GameProfileField::Gender, "count2 gender");
    result(GameProfileField::Gender, "2");
    Check(p.Snapshot().present == 3 && p.Snapshot().gender == 1, "source gender result swap");
    result(GameProfileField::Year, "2000");
    result(GameProfileField::Month, "2");
    p.Begin(GameProfileField::Day);
    std::string invalid = "30";
    Check(!p.Result(&invalid, 2026, error), "invalid Gregorian date cannot change result");
    Check(p.Result(nullptr, 2026, error) && p.Next() == GameProfileField::Month, "day cancel backs to month");
    result(GameProfileField::Month, "2");
    result(GameProfileField::Day, "29");
    Check(p.Snapshot().present == 5 && p.Snapshot().zodiac == 11 && p.Next() == GameProfileField::Blood,
          "birthday fills year AND zodiac,skips count4");
    Check(GameProfile::Days(1900, 2) == 28 && GameProfile::Days(2000, 2) == 29 &&
              GameProfile::Days(2024, 2) == 29 && GameProfile::Days(2100, 2) == 28,
          "Gregorian century rules");
    result(GameProfileField::Blood, "3");
    result(GameProfileField::Comment, "hello");
    Check(p.Snapshot().present == 7 && !p.Snapshot().applied && p.Next() == GameProfileField::Confirmation,
          "seven fields alone never apply profile");
    Check(p.Confirm(false) && !p.Snapshot().applied && !p.Snapshot().stored.live && p.Confirm(true, 3) &&
              p.Snapshot().applied,
          "TRC No preserves unapplied,only Yes applies");
    Check(p.Snapshot().stored.actor == 3 && p.Snapshot().stored.nickname == p.Snapshot().nickname &&
              p.Snapshot().stored.date == 0x01000229,
          "real typed profile store only after Yes");
    Check(GameProfile::PackDate(2026, 10, 21) == 0x01261021 &&
              GameProfile::PackDate(1899, 1, 1) == UINT32_MAX,
          "exact13F70/4224 date packing");
    for (const auto pair : {std::pair<int, int>{23, 6}, {24, 7}}) {
        GameProfile z;
        z.Begin(GameProfileField::Year);
        std::string year = "2000", month = "10", day = std::to_string(pair.first);
        Check(z.Result(&year, 2026, error), "zodiac year");
        z.Begin(GameProfileField::Month);
        Check(z.Result(&month, 2026, error), "zodiac month");
        z.Begin(GameProfileField::Day);
        Check(z.Result(&day, 2026, error) && z.Snapshot().zodiac == pair.second,
              "exact source Oct24 boundary");
    }
}
void BufferTests() {
    GameNameBuffer b;
    const std::string toro = "\x83\x67\x83\x8D";
    Check(b.Assign(toro, 4) && b.Units() == 2 && b.Cursor() == 2 && b.Bytes().size() == 4,
          "max counts source941C8 iterations,not bytes");
    Check(b.Insert("AB") && b.Units() == 4 && b.Bytes().size() == 6, "mixed single/double source units");
    const auto before = b.Bytes();
    Check(!b.Insert("C") && b.Bytes() == before && b.Cursor() == 4, "overlimit is atomic");
    b.Left();
    Check(b.Backspace() && b.Bytes() == toro + "B" && b.Cursor() == 2, "cursor/backspace removes whole unit");
    Check(b.Backspace() && b.Bytes() == std::string("\x83\x67") + "B" && b.Cursor() == 1,
          "multibyte deletion cannot leave a lead byte");
    b.Home();
    Check(b.Delete() && b.Bytes() == "B", "forward delete uses source boundary");
    b.End();
    Check(!b.Insert(std::string("\x83", 1)) && b.Bytes() == "B", "truncated DBCS rejected");
    Check(!b.Insert(std::string("\xB1", 1)) && b.Bytes() == "B",
          "Windows halfwidth single not silently paired by source decoder");
    Check(!b.Insert(std::string("\0", 1)) && !b.Insert("\n") && b.Bytes() == "B",
          "control/NUL cannot masquerade as source name");
    Check(b.Assign("", 4) && b.Insert(toro + toro) && b.Units() == 4,
          "four actual Japanese units/eight bytes");
    Check(!b.Insert("AB") && b.Units() == 4,
          "whole committed IME transaction rejected without partial change");
    Check(b.Assign("AB", 4) && !b.Assign(toro + toro + "X", 4) && b.Bytes() == "AB" && b.Maximum() == 4,
          "invalid reassign preserves owner value");
}
void InputTests() {
    GameTextKeyGate gate;
    gate.Context(1, true);
    Check(!gate.KeyDown('X', true) && !gate.Character('X'), "held Circle key cannot type in new context");
    Check(gate.KeyDown('X', false) && gate.Character('X'), "fresh typing key admitted");
    gate.Context(2, true);
    Check(!gate.Character('X') && !gate.KeyDown('X', true), "old context/queued repeat rejected");
    gate.KeyUp('X');
    Check(gate.KeyDown('X', false), "fresh release/press can type");
    gate.Focus(false);
    gate.Focus(true);
    Check(!gate.KeyDown('X', true), "refocus does not restore held key admission");
    gate.Context(0, true);
    Check(!gate.KeyDown(13, false), "retired/no editor context cannot confirm");
    gate.Context(3,true);
    Check(gate.KeyDown(0xE7,false),"fresh owned VK_PACKET key");const auto packet_epoch=gate.Epoch();
    Check(gate.TranslatedCharacter(0xE7,3,packet_epoch),"owned VK_PACKET translation uses the actual key,not a guessed scan mapping");
    Check(!gate.TranslatedCharacter(0xE7,0,packet_epoch)&&!gate.TranslatedCharacter(0xE7,2,packet_epoch),"unowned/old translated character cannot adopt current editor");
    gate.KeyUp(0xE7);Check(!gate.TranslatedCharacter(0xE7,3,packet_epoch),"translation must be delivered within its admitted key transaction");
    gate.Context(4,true);Check(!gate.KeyDown(0xE7,true)&&!gate.TranslatedCharacter(0xE7,3,packet_epoch),"new editor refuses old packet/repeat");
    Check(gate.KeyDown('A',false),"fresh ordinary letter key");const auto letter_epoch=gate.Epoch();
    Check(gate.TranslatedCharacter('A',4,letter_epoch),"ordinary translated key keeps the same ownership guard");
    gate.KeyDown('B',false);Check(!gate.TranslatedCharacter('A',4,letter_epoch),"a nested key admission invalidates an earlier association");
    gate.Focus(false);Check(!gate.TranslatedCharacter('A',4,letter_epoch),"focus loss retires translated input admission");
    gate.Focus(true);Check(gate.KeyDown('A',false)&&!gate.TranslatedCharacter('A',4,letter_epoch),"a new key after refocus cannot revive the old association");
    gate.Context(5,true);gate.Context(4,true);gate.KeyDown('A',false);
    Check(!gate.TranslatedCharacter('A',4,letter_epoch),"context ABA cannot revive an old association");
    gate.Context(0,true);Check(!gate.TranslatedCharacter('A',4,letter_epoch),"retired editor rejects pending text");
    GameTextMailbox mailbox;
    GameTextEvent event;
    event.context = 7;
    event.kind = GameTextKind::Confirm;
    for (unsigned i = 0; i < 128; ++i)
        Check(mailbox.Push(event), "bounded text queue accepts capacity");
    Check(!mailbox.Push(event), "overflow reports refusal");
    GameTextEvent got;
    Check(mailbox.Pop(got) && got.kind == GameTextKind::Resync && got.context == 7 && !mailbox.Pop(got),
          "overflow drops stale confirmations and requires focus resync");
    mailbox.Clear();
    event.bytes = 65;
    Check(!mailbox.Push(event) && !mailbox.Pop(got), "invalid fixed extent rejected");
}
void LifecycleTests() {
    GameNameEntry editor;
    Check(editor.Begin("AB", 4) && editor.Snapshot().source_state == 0,
          "1FD80 opens without editor/result completion");
    auto context = editor.Snapshot().context;
    const auto event = [&](GameTextKind kind, std::string_view text = {}) {
        GameTextEvent e;
        e.context = context;
        e.kind = kind;
        e.bytes = uint16_t(text.size());
        std::copy(text.begin(), text.end(), e.text.begin());
        return e;
    };
    Check(editor.Apply(event(GameTextKind::FocusGained)) && !editor.Apply(event(GameTextKind::Confirm)),
          "opening cannot accept queued confirm");
    editor.ControlCompleted(1);
    Check(editor.Snapshot().source_state == 0, "open still waits actual mode2");
    editor.ControlCompleted(2);
    Check(editor.Snapshot().source_state == 1, "open completion enables edits");
    Check(editor.Apply(event(GameTextKind::Backspace)) &&
              editor.Apply(event(GameTextKind::Commit, "\x83\x67")),
          "real staged single/double-byte edits");
    Check(editor.Snapshot().staged == std::string("A\x83\x67") && editor.Snapshot().source120 == "AB",
          "staged172 differs from source committed120");
    Check(editor.Apply(event(GameTextKind::CompositionBegin)) && !editor.Apply(event(GameTextKind::Confirm)),
          "partial IME cannot accept");
    Check(editor.Apply(event(GameTextKind::Commit, "C")) && editor.Apply(event(GameTextKind::CompositionEnd)),
          "only committed IME text enters staging");
    Check(editor.Apply(event(GameTextKind::FocusLost)) && !editor.Apply(event(GameTextKind::Confirm)),
          "unfocused confirm refused");
    Check(editor.Apply(event(GameTextKind::FocusGained)) && editor.Apply(event(GameTextKind::Confirm)),
          "explicit native confirm copies staged name");
    Check(editor.Snapshot().source_state == 2 && !editor.Result(),
          "200F0 begins close;no caller-visible result yet");
    editor.ControlCompleted(1);
    Check(editor.Snapshot().source_state == 2, "close completion is2,not0/forced timer");
    editor.ControlCompleted(2);
    Check(editor.Snapshot().source_state == 3 && editor.Result() &&
              *editor.Result() == std::string("A\x83\x67"
                                              "C"),
          "22A78 yields actual accepted name after closing");
    const auto old = event(GameTextKind::Confirm);
    Check(editor.Begin("OLD", 4), "new dialog can open after prior completion");
    context = editor.Snapshot().context;
    Check(!editor.Apply(old) && editor.Snapshot().source_state == 0, "old context cannot accept new name");
    editor.ControlCompleted(2);
    Check(editor.Apply(event(GameTextKind::FocusGained)) && editor.Apply(event(GameTextKind::Cancel)),
          "source cancel begins close");
    editor.ControlCompleted(2);
    Check(!editor.Result() && editor.Snapshot().cancelled && editor.Snapshot().source120.empty(),
          "cancel clears source120 and returns null");
    Check(editor.Begin("", 4), "empty initial playername is not fabricated");
    context = editor.Snapshot().context;
    editor.ControlCompleted(2);
    Check(editor.Apply(event(GameTextKind::FocusGained)) && editor.Apply(event(GameTextKind::Confirm)),
          "source empty accept may close");
    editor.ControlCompleted(2);
    Check(!editor.Result(), "22A78 empty result remains null");
    Check(editor.Begin("12345678",8),"Input32 source mode0 character capacity");
    context=editor.Snapshot().context;editor.ControlCompleted(2);
    Check(editor.Apply(event(GameTextKind::FocusGained)),"Input32 focus");
    const auto ninth=event(GameTextKind::Commit,"9");uint32_t remaining=1;
    if(GameInput32KeyboardActivity(editor.Snapshot(),ninth))remaining=9000;
    Check(!editor.Apply(ninth)&&editor.Snapshot().staged=="12345678"&&remaining==9000,
          "rejected capacity edit is still real keyboard activity before timeout");
    auto stale=ninth;--stale.context;
    Check(!GameInput32KeyboardActivity(editor.Snapshot(),stale),"foreign editor context cannot reset inactivity");
    Check(editor.Apply(event(GameTextKind::CompositionBegin))&&editor.Apply(event(GameTextKind::FocusLost)),"Input32 interrupted composition");
    Check(!GameInput32KeyboardActivity(editor.Snapshot(),ninth),"unfocused key cannot reset inactivity");
    Check(editor.Expire()&&editor.Snapshot().source_state==2&&!editor.Result(),"timeout starts actual close despite focus/composition");
    editor.ControlCompleted(1);Check(!editor.Result()&&!editor.Snapshot().result_ready,"timeout cannot publish before close2");
    editor.ControlCompleted(2);Check(editor.Snapshot().result_ready&&!editor.Result(),"timeout nil only after close producer");
    Check(!GameInput32KeyboardActivity(editor.Snapshot(),ninth),"closing/retired context cannot reset idle clock");
    editor.Retire();
    Check(!editor.Snapshot().live && !editor.Apply(event(GameTextKind::Commit, "X")),
          "retired owner rejects edits");
}
#ifdef _WIN32
void EncodingTests() {
    std::string out, error;
    Check(dokodemo::EncodeGameText932(L"トロAB", out, error) && out == "\x83\x67\x83\x8D"
                                                                       "AB",
          "actual Windows CP932 fullwidth/ASCII conversion");
    const auto old = out;
    Check(!dokodemo::EncodeGameText932(L"\xD83D\xDC31", out, error) && out == old,
          "unrepresentable scalar never replacement question mark");
    Check(!dokodemo::EncodeGameText932(std::wstring(1, wchar_t(0xD800)), out, error) && out == old,
          "partial IME UTF16 surrogate not committed");
    Check(!dokodemo::EncodeGameText932(L"\x00A5", out, error) && out == old,
          "best-fit lossy spelling refused");
}
#endif
} // namespace
int main() {
    try {
        WindowButtonsTests();
        BufferTests();
        InputTests();
        LifecycleTests();
        ProfileTests();
#ifdef _WIN32
        EncodingTests();
        game_text_transaction_tests::Run();
#endif
        std::cout << "source-unit name editing,context/focus,atomic committed text and encoding PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
