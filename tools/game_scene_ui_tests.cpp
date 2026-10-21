#include "recovered/game_boot_native.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace dokodemo::recovered;
namespace {
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void ChoiceTests(){
    GameChoiceController choice;uint64_t sequence=0;
    const auto input=[&](uint32_t keys,bool focus=true,uint64_t context=20,uint64_t owner=4){return choice.Input({keys,focus,true,++sequence,0,context,owner});};
    Check(choice.Begin({"one","two","three"},20,4),"actual three option owning request");
    Check(!input(Circle)&&choice.Snapshot().remaining==5400,"opening ignores prior input and timeout");
    choice.Tick(1);Check(choice.Snapshot().state==0,"opening control still running");
    choice.Tick(2);Check(choice.Snapshot().state==1&&choice.Snapshot().active_updates==0,"open completion enters edit without same tick deadline/input");
    Check(!input(0,true,19)&&!input(Circle,true,19),"old sampled message cannot arm SELECT");
    input(Circle);choice.Tick(2);Check(choice.Snapshot().state==1,"held prior Circle refuses");
    input(0);choice.Tick(2);input(Cross);choice.Tick(2);Check(choice.Snapshot().state==1,"source cancelByte0 ignores Cross");
    input(0);choice.Tick(2);input(Down);choice.Tick(2);Check(choice.Snapshot().row==1,"fresh Down chooses second source option");
    for(unsigned i=0;i<14;++i){input(Down);choice.Tick(2);}Check(choice.Snapshot().row==2,"held repeat clamps at bottom");
    input(0);choice.Tick(2);input(Down);choice.Tick(2);Check(choice.Snapshot().row==0,"fresh edge wraps at bottom");
    input(0);choice.Tick(2);input(Circle,false);choice.Tick(2);input(Circle,true);choice.Tick(2);Check(choice.Snapshot().state==1,"refocused-held cannot select");
    input(0);choice.Tick(2);input(Circle);choice.Tick(2);Check(choice.Snapshot().state==2&&choice.Snapshot().result==1,"fresh confirm chooses actual row but is not completion");
    choice.Tick(1);Check(choice.Snapshot().state==2,"close control must really finish");
    choice.Tick(2);Check(choice.Snapshot().state==3&&choice.Snapshot().confirmations==1,"closed SELECT publishes state3 once");
    Check(choice.Begin({"a","b"},21,4,1),"separate explicit one-tick timeout fixture");
    choice.Tick(2);const auto before=choice.Snapshot();choice.Tick(2,false);Check(choice.Snapshot().remaining==before.remaining,"nonidle TRC suspends deadline");
    choice.Input({0,true,true,++sequence,0,21,4});choice.Input({Circle,true,true,++sequence,0,21,4});choice.Tick(2);
    Check(choice.Snapshot().state==2&&choice.Snapshot().result==0&&choice.Snapshot().timed_out&&choice.Snapshot().confirmations==0,"expiry wins simultaneous confirm and returns zero");
    choice.Retire();Check(!choice.Input({0,true,true,++sequence,0,21,4}),"retired SELECT rejects queued input");
}
void W(std::vector<uint8_t> &b, size_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        b.at(at + i) = uint8_t(value >> (8 * i));
}
void H(std::vector<uint8_t> &b, size_t at, uint16_t value) {
    b.at(at) = uint8_t(value);
    b.at(at + 1) = uint8_t(value >> 8);
}
struct Lease : PackageStorageLease {
    std::vector<uint8_t> bytes, known;
    bool live = true;
    explicit Lease(std::vector<uint8_t> b) : bytes(std::move(b)), known(bytes.size(), 1) {}
    bool Span(PackageStorageSpan &span, std::string &error) const override {
        if (!live) {
            error = "fixture retired";
            return false;
        }
        span.bytes = const_cast<uint8_t *>(bytes.data());
        span.initialized = const_cast<uint8_t *>(known.data());
        span.size = bytes.size();
        span.host_address = uint64_t(reinterpret_cast<uintptr_t>(bytes.data()));
        span.owner = span.host_address;
        span.generation = span.serial = 1;
        span.readable = span.writable = true;
        return true;
    }
};
PackageView View(std::vector<uint8_t> b) {
    const auto size = b.size();
    return PackageStorage::Attach(std::make_shared<Lease>(std::move(b)), size);
}
std::vector<uint8_t> Gim() {
    // Explicit tiny single-picture RGBA fixture, not original game pixels.
    std::vector<uint8_t> b(144, 0);
    const uint8_t magic[]{'M', 'I', 'G', '.', '0', '0', '.', '1', 'P', 'S', 'P', 0};
    std::copy(std::begin(magic), std::end(magic), b.begin());
    for (size_t at : {16u, 32u}) {
        H(b, at, at == 16 ? 2 : 3);
        H(b, at + 2, 16);
        W(b, at + 4, uint32_t(b.size() - at));
        W(b, at + 8, 16);
        W(b, at + 12, 16);
    }
    H(b, 48, 4);
    H(b, 50, 16);
    W(b, 52, 96);
    W(b, 56, 96);
    W(b, 60, 16);
    const size_t r = 64;
    H(b, r, 48);
    H(b, r + 4, 3);
    H(b, r + 8, 2);
    H(b, r + 10, 2);
    H(b, r + 12, 32);
    H(b, r + 14, 4);
    H(b, r + 16, 1);
    W(b, r + 24, 48);
    W(b, r + 28, 64);
    W(b, r + 32, 80);
    H(b, r + 40, 1);
    H(b, r + 42, 1);
    H(b, r + 44, 3);
    H(b, r + 46, 1);
    W(b, r + 48, 64);
    W(b, 128, 0xFF0000FF);
    W(b, 132, 0xFF00FF00);
    W(b, 136, 0xFFFF0000);
    W(b, 140, 0xFFFFFFFF);
    return b;
}
std::vector<uint8_t> Ui() {
    std::vector<uint8_t> b(32 + 200 * 8 + 26 * 52, 0);
    W(b, 0, 0x30413242);
    W(b, 4, 0);
    W(b, 8, 32);
    W(b, 12, uint32_t(-200));
    W(b, 16, 32);
    W(b, 20, uint32_t(-26));
    W(b, 24, 32 + 200 * 8);
    W(b, 28, 10);
    return b;
}
std::vector<uint8_t> Nj() {
    std::vector<uint8_t> b(64, 0);
    const auto be = [&](size_t at, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            b[at + i] = uint8_t(value >> (24 - i * 8));
    };
    be(0, 0x4E4A4443);
    be(4, 0x10000);
    be(8, 1);
    be(12, 8);
    be(16, 28);
    be(20, 40);
    be(24, 40);
    be(32, 12345);
    be(36, 23456);
    be(60, 0x4E4A4443);
    return b;
}
std::vector<uint8_t> Font() {
    std::vector<uint8_t> b(96, 0);
    W(b, 0, 0x31544658);
    W(b, 4, 0x00100010);
    W(b, 24, 1);
    W(b, 32, 4);
    W(b, 36, 4);
    W(b, 40, 16);
    W(b, 44, 64);
    W(b, 52, 8);
    W(b, 56, 80);
    return b;
}
std::vector<uint8_t> Atlas() {
    std::vector<uint8_t> b(320, 0);
    W(b, 0, 0x31544658);
    W(b, 4, 0x00020002);
    W(b, 8, 64);
    W(b, 12, 1);
    W(b, 16, 60);
    W(b, 20, 0x00080040);
    W(b, 24, 1);
    W(b, 28, 4);
    W(b, 32, 16);
    W(b, 36, 16);
    W(b, 40, 128);
    W(b, 44, 128);
    W(b, 48, 3);
    W(b, 52, 16);
    W(b, 56, 256);
    std::fill(b.begin() + 60, b.begin() + 124, uint8_t(2));
    std::fill(b.begin() + 128, b.begin() + 256, uint8_t(0x11));
    W(b, 256 + 4, 0xFFFFFFFF);
    return b;
}
struct Resources : GameResourceSource {
    bool fail_dictionary = false;
    uint32_t requests = 0;
    GameFsStatus OpenResource(const std::string &path, PackageView &out) override {
        ++requests;
        if (path.find(".CB2A") != std::string::npos)
            out = View(Ui());
        else if (path.find(".GIM") != std::string::npos)
            out = View(Gim());
        else if (path.find(".A") != std::string::npos) {
            if (fail_dictionary)
                return {GameFsCode::NotFound, 0, "explicit missing dictionary fixture"};
            out = View(Nj());
        } else
            return {GameFsCode::NotFound, 0, "fixture selection"};
        return {};
    }
};
void ImageTests() {
    GameImage image;
    Check(bool(image.DecodeGim(View(Gim()))) && image.Width() == 2 && image.Height() == 2,
          "checked GIM decode");
    Check(image.Pixels() == std::vector<uint32_t>({0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF}),
          "ABGR source to native ARGB");
    auto mip = Gim();
    mip.resize(148);
    W(mip, 20, 132);
    W(mip, 36, 116);
    W(mip, 52, 100);
    W(mip, 56, 100);
    W(mip, 96, 84);
    H(mip, 106, 2);
    W(mip, 116, 80);
    W(mip, 144, 0xFF808080);
    Check(bool(image.DecodeGim(View(mip))) && image.Width() == 2 && image.Pixels()[0] == 0xFFFF0000,
          "all mip extents checked,native base-level sample retains actual pixels");
    W(mip, 116, 1000);
    Check(!image.DecodeGim(View(mip)), "unused lower mip still must have valid extent");
    for (unsigned bad = 0; bad < 4; ++bad) {
        auto b = Gim();
        if (bad == 0)
            b[0] = 0;
        if (bad == 1)
            W(b, 52, UINT32_MAX);
        if (bad == 2)
            H(b, 108, 2);
        if (bad == 3)
            W(b, 64 + 32, 200);
        Check(!image.DecodeGim(View(b)) && image.Pixels().empty(),
              "malformed/unsupported raster not success");
    }
    GameGraphics graphics;
    Check(graphics.Initialize({}) && graphics.Clear(0xFF223344), "native surface");
    Check(bool(image.DecodeGim(View(Gim()))) && graphics.DrawImage(image.Pixels(), 2, 2, 3, 4) &&
              graphics.SwapBuffers(),
          "actual decoded-pixel draw");
    GameGraphicsFrame frame;
    Check(graphics.CopyFront(frame) && frame.pixels[4 * frame.stride + 3] == 0xFFFF0000,
          "image blit reaches framebuffer");
    GameTransition fade;
    Check(fade.Initialize({{0, 0}}) && fade.Snapshot().radius == -50, "source fade defaults");
    Check(graphics.Clear(0xFFFFFFFF) && fade.Draw(graphics) && graphics.SwapBuffers() &&
              graphics.CopyFront(frame) && frame.pixels[136 * frame.stride + 240] == 0xFF000000,
          "cold closed fade really covers surface");
    fade.Open();
    for (unsigned i = 0; i < 40; ++i)
        fade.Update();
    Check(fade.Snapshot().state == 2, "fade reaches open,not premature success");
    Check(graphics.Clear(0xFFFFFFFF) && fade.Draw(graphics) && graphics.SwapBuffers() &&
              graphics.CopyFront(frame) && frame.pixels[136 * frame.stride + 240] == 0xFFFFFFFF,
          "fully open source branch skips overlay");
}
void OwnerTests() {
    std::vector<uint8_t> b(1024 * 1024, 0x5A);
    GameArena arena;
    Check(arena.Initialize(b.data(), uint32_t(b.size())), "arena");
    GameVmBoot vm;
    Check(vm.Initialize74EF4(arena) && vm.InitializeManager93404(), "manager");
    const auto baseline = arena.Used();
    GameFontResource font;
    Check(bool(font.Initialize(View(Font()))), "live font prerequisite");
    for (bool fail : {true, false}) {
        Resources source;
        source.fail_dictionary = fail;
        GameSceneUiBootstrap scene;
        const auto result = scene.Initialize(source, arena, vm, font);
        Check(bool(result) != fail, "real partial failure is not ctor success");
        if (!fail) {
            const auto s = scene.Snapshot();
            Check(s.owners == 5 && s.records == 8 && s.ui_resources == 3 && s.borrowed_dictionaries == 7 &&
                      s.mutable_dictionaries == 2 && s.text_capacity_bytes == 91916 && !s.visible_roots &&
                      !s.ui_render_implemented,
                  "grouped owner/resource state");
        }
        Check(!vm.Close(), "parent cannot retire child tasks");
        Check(scene.Close() && arena.Used() == baseline,
              "partial and complete owners release every buffer/task");
    }
    GameDictionary dictionary;
    Check(dictionary.InitializeMutable(arena, 1) && dictionary.Snapshot().capacity == 961 &&
              dictionary.ReadBE32(8) == 0x80020000 && dictionary.ReadBE32(32) == 1980,
          "source mutable mode1 exact header");
    Check(dictionary.Close() && dictionary.InitializeMutable(arena, 2) &&
              dictionary.Snapshot().capacity == 100 && dictionary.ReadBE32(44) == 85,
          "source mutable mode2 distinct owner");
    Check(dictionary.Close() && vm.Close() && !arena.Used(), "all ownership retired");
    Check(bool(dictionary.Attach(View(Nj()), 3)),
          "NJ20/24 decimal bounded fields;32/36 table offsets not small limits");
    Check(dictionary.Close(), "borrowed dictionary detach");
    auto malformed = Nj();
    malformed[23] = 41;
    Check(!dictionary.Attach(View(malformed), 0), "actual decimal20 limit rejection");
}
void BankTests() {
    std::vector<uint8_t> b(24, 0);
    W(b, 0, 0x44584753);
    W(b, 8, 24);
    W(b, 16, 0x444E4752);
    W(b, 20, 0);
    GameSoundBank bank;
    Check(bool(bank.Register(View(b))) && bank.Snapshot().native_metadata_slot == 63 &&
              bank.Snapshot().regions && !bank.Snapshot().mixer_ready,
          "real metadata slot distinct from mixer");
    bank.Close();
    W(b, 20, 1);
    Check(!bank.Register(View(b)) && !bank.Snapshot().metadata_ready, "bank chunk overflow refused");
}
void FontDrawTests() {
    GameFontResource font;
    Check(bool(font.Initialize(View(Atlas()))), "finite actual-format atlas fixture");
    GameFontDrawRecord records[2];
    GameFontBitmap bitmap;
    Check(bool(font.RasterizeCp932({"AA"}, 16, bitmap, records, 2)) && bitmap.glyphs == 2 &&
              bitmap.width == 16 && bitmap.height == 23 && bitmap.pixels[0] == 0xFF552F0D &&
              bitmap.pixels[4] == 0xFF552F0D && bitmap.pixels[3] == 0,
          "source glyph widths,spacing,INDEX4 palette and source color");
    Check(records[0].page == 0 && records[0].width == 2 && records[0].height == 2 && records[1].x == 4 &&
              records[0].u == 2 && records[0].v == 8,
          "actual typed44-byte draw records are populated and consumed");
    Check(!font.RasterizeCp932({"AAA"}, 16, bitmap, records, 2), "glyph owner capacity cannot overflow");
    Check(bool(font.RasterizeCp932({"A"}, 16, bitmap, records, 2, 0x80204060)) &&
              bitmap.pixels[0] == 0x80204060,
          "TITLE uses its own live glyph color and fade alpha,not TRC hardcoded "
          "color");
    Check(!font.RasterizeCp932({std::string("\x83", 1)}, 16, bitmap), "incomplete source CP932 rejected");
    auto b = Atlas();
    W(b, 52, 17);
    font.Close();
    Check(bool(font.Initialize(View(b))) && !font.RasterizeCp932({"A"}, 16, bitmap),
          "unreached palette domain never generic success");
}
void ChoiceDrawTests() {
    std::vector<uint8_t> storage(1024*1024);
    GameArena arena;Check(arena.Initialize(storage.data(),uint32_t(storage.size())),"SELECT draw arena");
    GameVmBoot vm;Check(vm.Initialize74EF4(arena)&&vm.InitializeManager93404(),"SELECT draw manager");
    GameFontResource font;Check(bool(font.Initialize(View(Atlas()))),"SELECT draw atlas");
    Resources source;GameGraphics graphics;Check(graphics.Initialize({}),"SELECT draw surface");
    std::vector<uint32_t> plain;
    const std::vector<std::string> options{"A",std::string("\x16")+"A\x16",std::string("\x14")+"A",std::string("\x83",1)};
    for(size_t i=0;i<options.size();++i){
        GameSceneUiBootstrap scene;Check(bool(scene.Initialize(source,arena,vm,font)),"SELECT draw owners");
        Check(bool(scene.BeginChoice({options[i]},20+i,4)),"owning raw SELECT option");
        Check(graphics.Clear(0xFF000000),"SELECT draw clear");
        Check(scene.DrawChoice(graphics)==(i<2),"only highlight markers are suppressed; unknown controls and malformed CP932 refuse");
        Check(scene.Choice().options[0]==options[i],"display projection preserves owning raw option");
        if(i<2){
            GameGraphicsFrame frame;Check(graphics.SwapBuffers()&&graphics.CopyFront(frame),"SELECT draw publication");
            if(i==0)plain=frame.pixels;
            else Check(frame.pixels==plain,"highlight projection reaches the same actual glyph raster");
        }
        Check(scene.Close(),"SELECT draw owners retire");
    }
    Check(vm.Close()&&!arena.Used(),"SELECT draw registry and buffers retired");
}
void UiDrawTests() {
    struct DrawResources : Resources {
        GameFsStatus OpenResource(const std::string &path, PackageView &out) override {
            if (path.find(".CB2A") == std::string::npos)
                return Resources::OpenResource(path, out);
            std::vector<uint8_t> b(144, 0);
            W(b, 0, 0x30413242);
            W(b, 4, 1);
            W(b, 8, 32);
            W(b, 12, 2);
            W(b, 16, 40);
            W(b, 20, 1);
            W(b, 24, 56);
            W(b, 28, 1);
            H(b, 32, uint16_t(-1));
            H(b, 34, uint16_t(-1));
            W(b, 36, 112);
            const char name[] = "fixture.GIM";
            std::copy(name, name + sizeof(name), b.begin() + 112);
            H(b, 42, 1);
            W(b, 44, 14);
            W(b, 52, 14);
            W(b,104,2); //source control end,independent of unconsumed clip+28
            out = View(std::move(b));
            return {};
        }
    } source;
    auto resource = std::make_shared<GameUiResource>();
    Check(bool(resource->Load(source, "draw.CB2A", "")), "checked draw resource");
    GameUiInstance instance;
    Check(instance.Clone(resource) && instance.ShowRoot(0), "live UI clone");
    auto *root = instance.Node(0);
    root->position = {{20, 30}};
    root->scale = {{2, 2}};
    auto *child = instance.Node(1);
    child->position = {{3, 4}};
    child->texture = 1;
    Check(instance.SetSourceCallback(1, 0x1234), "typed callback binding");
    struct Callbacks : GameUiCallbacks {
        GameUiMatrix matrix;
        uint32_t count = 0;
        bool DrawUiCallback(uint32_t pc, uint32_t node, const GameUiMatrix &m, uint32_t, GameGraphics &,
                            std::string &) override {
            Check(pc == 0x1234 && node == 1, "exact callback identity");
            matrix = m;
            ++count;
            return true;
        }
    } callbacks;
    GameGraphics graphics;
    Check(graphics.Initialize({}) && graphics.Clear(0xFF000000), "draw surface");
    GameUiDrawStats stats;
    std::string error;
    Check(instance.Draw(graphics, stats, error, &callbacks) && stats.quads == 1 && stats.callbacks == 1 &&
              callbacks.matrix.tx == 26 && callbacks.matrix.ty == 38 && callbacks.matrix.a == 2,
          "source low flags2/4 retain parent world,append local transform");
    Check(instance.Draw(graphics, stats, error, &callbacks) && callbacks.matrix.tx == 26 &&
              callbacks.matrix.ty == 38,
          "second frame recomputes parent transform without cached accumulation");
    Check(!instance.Draw(graphics, stats, error), "reached callback cannot silently become no-op");
    Check(instance.SetSourceCallback(1, 0) && graphics.SwapBuffers(), "remove fixture callback");
    GameGraphicsFrame frame;
    Check(graphics.CopyFront(frame) && frame.pixels[172 * frame.stride + 264] != 0xFF000000,
          "negative texture origin subtracts locally and actual GIM reaches "
          "framebuffer");
    child->texture = -1;
    Check(!instance.Draw(graphics, stats, error), "negative-selector recursive reference rejects cycle");
    child->texture = 2;
    Check(!instance.Draw(graphics, stats, error), "out-of-range texture is error");
    child->texture=1;
    auto* control=instance.AddControl(0);Check(control!=nullptr,"animation control owner");
    control->mode=control->active=1;
    Check(instance.Update(error)&&control->frame==1&&control->mode==1,"96730 frame<end-1 advances without premature result");
    Check(instance.Update(error)&&control->frame==2&&control->mode==2,"96730 equality advances AND completes on this tick");
    control->frame=3;control->mode=1;
    Check(instance.Update(error)&&control->frame==3&&control->mode==1,"96730 beyond-end does not invent completion");
    control->active=0;
    instance.ShowRoot(0,false);
    Check(instance.Draw(graphics,stats,error)&&stats.nodes==0&&instance.Draw(graphics,stats,error,nullptr,{},0,UINT32_MAX,0)&&stats.quads==1,"32B04 manual root draw independent of B8 visible flag");
    instance.Close();
    resource->Close();
    Check(!instance.Draw(graphics, stats, error), "retired UI cannot draw");
}
void DialogInputTests() {
    GameDialogController dialog;
    Check(dialog.Begin(), "source TRC prompt begins opening");
    for (unsigned i = 0; i < 5; ++i)
        dialog.Tick();
    Check(dialog.Snapshot().state == 1 && dialog.Snapshot().progress == 1,
          "opening reaches input wait naturally");
    Check(!dialog.Input({Circle, true, false, 1}), "held on initial wait not accepted");
    Check(!dialog.Input({0, false, false, 2}) && !dialog.Input({Circle, true, false, 3}),
          "unfocused release cannot arm confirmation");
    Check(!dialog.Input({Cross, true, false, 4}) && dialog.Snapshot().state == 1,
          "cold TRC cancel-disabled ignores Cross");
    Check(!dialog.Input({0, true, false, 4}) && dialog.Input({Circle, true, false, 5}),
          "focused release/first press triggers48224");
    Check(dialog.Snapshot().result == 1 && dialog.Snapshot().state == 2 && dialog.Snapshot().progress == 1,
          "result producer precedes genuine closing,never forced state3");
    dialog.Tick();
    Check(dialog.Snapshot().state == 2 && dialog.Snapshot().progress < 1,
          "closing is a real animation state");
    for (unsigned i = 0; i < 10 && dialog.Snapshot().state != 3; ++i)
        dialog.Tick();
    Check(dialog.Snapshot().state == 3 && !dialog.Snapshot().progress && dialog.Snapshot().confirmations == 1,
          "natural zero progress completes");
    Check(!dialog.Input({Circle, true, false, 6}) && dialog.Begin(1, 1) &&
              !dialog.Input({Circle, true, false, 7}),
          "no retired/held-key next prompt confirmation");
    for (unsigned i = 0; i < 5; ++i)
        dialog.Tick();
    Check(!dialog.Input({0, true, false, 8}) && dialog.Input({Circle, true, false, 9}) &&
              dialog.Snapshot().result == 2,
          "selection+1 source result");
    dialog.Retire();
    Check(dialog.Begin(2), "mode2 prompt can open");
    for (unsigned i = 0; i < 5; ++i)
        dialog.Tick();
    Check(!dialog.Input({0, true, false, 10}) && !dialog.Input({Circle, true, false, 11}),
          "source mode2 suppresses Circle");
}
} // namespace
int main() {
    ChoiceTests();
    try {
        ImageTests();
        OwnerTests();
        BankTests();
        FontDrawTests();
        ChoiceDrawTests();
        DialogInputTests();
        UiDrawTests();
        std::cout << "scene UI resources,decoded pixels,source XFT text,fade,NJ "
                     "and partial retirement PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
