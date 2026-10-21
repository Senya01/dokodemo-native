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
    auto mip=Gim();mip.resize(148);W(mip,20,132);W(mip,36,116);W(mip,52,100);W(mip,56,100);
    W(mip,96,84);H(mip,106,2);W(mip,116,80);W(mip,144,0xFF808080);
    Check(bool(image.DecodeGim(View(mip)))&&image.Width()==2&&image.Pixels()[0]==0xFFFF0000,
          "all mip extents checked,native base-level sample retains actual pixels");
    W(mip,116,1000);
    Check(!image.DecodeGim(View(mip)),"unused lower mip still must have valid extent");
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
    Check(!font.RasterizeCp932({std::string("\x83", 1)}, 16, bitmap), "incomplete source CP932 rejected");
    auto b = Atlas();
    W(b, 52, 17);
    font.Close();
    Check(bool(font.Initialize(View(b))) && !font.RasterizeCp932({"A"}, 16, bitmap),
          "unreached palette domain never generic success");
}
void DialogInputTests() {
    GameDialogController dialog;Check(dialog.Begin(),"source TRC prompt begins opening");
    for(unsigned i=0;i<5;++i)dialog.Tick();
    Check(dialog.Snapshot().state==1&&dialog.Snapshot().progress==1,"opening reaches input wait naturally");
    Check(!dialog.Input({Circle,true,false,1}),"held on initial wait not accepted");
    Check(!dialog.Input({0,false,false,2})&&!dialog.Input({Circle,true,false,3}),"unfocused release cannot arm confirmation");
    Check(!dialog.Input({Cross,true,false,4})&&dialog.Snapshot().state==1,"cold TRC cancel-disabled ignores Cross");
    Check(!dialog.Input({0,true,false,4})&&dialog.Input({Circle,true,false,5}),"focused release/first press triggers48224");
    Check(dialog.Snapshot().result==1&&dialog.Snapshot().state==2&&dialog.Snapshot().progress==1,
        "result producer precedes genuine closing,never forced state3");
    dialog.Tick();Check(dialog.Snapshot().state==2&&dialog.Snapshot().progress<1,"closing is a real animation state");
    for(unsigned i=0;i<10&&dialog.Snapshot().state!=3;++i)dialog.Tick();
    Check(dialog.Snapshot().state==3&&!dialog.Snapshot().progress&&dialog.Snapshot().confirmations==1,"natural zero progress completes");
    Check(!dialog.Input({Circle,true,false,6})&&dialog.Begin(1,1)&&!dialog.Input({Circle,true,false,7}),"no retired/held-key next prompt confirmation");
    for(unsigned i=0;i<5;++i)dialog.Tick();
    Check(!dialog.Input({0,true,false,8})&&dialog.Input({Circle,true,false,9})&&dialog.Snapshot().result==2,"selection+1 source result");
    dialog.Retire();Check(dialog.Begin(2),"mode2 prompt can open");for(unsigned i=0;i<5;++i)dialog.Tick();
    Check(!dialog.Input({0,true,false,10})&&!dialog.Input({Circle,true,false,11}),"source mode2 suppresses Circle");
}
} // namespace
int main() {
    try {
        ImageTests();
        OwnerTests();
        BankTests();
        FontDrawTests();DialogInputTests();
        std::cout
            << "scene UI resources,decoded pixels,source XFT text,fade,NJ and partial retirement PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
