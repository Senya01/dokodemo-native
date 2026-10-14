#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "asset_store.h"
#include "save_native.h"
#include "save_time.h"
#include "script_vm.h"
#include "audio_native.h"
#include "game_font.h"
#include "model_formats.hpp"
#include "startup_probe.h"
#include "audio_catalog.h"
#include <shellapi.h>
#include <mmsystem.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iostream>
#include <memory>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

using namespace native_port;
namespace {
struct Options { bool selfTest = false, smoke = false; std::filesystem::path assets, evidence; };
Options ParseOptions() {
    Options options;
    int count = 0; auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; i < count; ++i) {
        std::wstring arg = args[i];
        if (arg == L"--self-test") options.selfTest = true;
        else if (arg == L"--smoke-test") options.smoke = true;
        else if (arg == L"--assets" && i + 1 < count) options.assets = args[++i];
        else if (arg == L"--evidence" && i + 1 < count) options.evidence = args[++i];
        else { LocalFree(args); throw std::runtime_error("Unknown or incomplete command line argument"); }
    }
    LocalFree(args);
    options.assets = ResolveAssets(options.assets);
    if (options.evidence.empty()) options.evidence = std::filesystem::current_path() / L"evidence";
    options.evidence = std::filesystem::absolute(options.evidence);
    return options;
}
void Output(const std::string& text) {
    DWORD written = 0; auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle && handle != INVALID_HANDLE_VALUE) WriteFile(handle, text.data(), (DWORD)text.size(), &written, nullptr);
    OutputDebugStringW(Utf8(text).c_str());
}
bool VerifiedStartupPrefix(const dokodemo::startup::Trace& trace, const std::string& source) {
    // Expected endpoints come from the bounded original-word/KSC traces. The
    // execution budget is independent of the observed command count.
    const std::map<std::string,uint32_t> stops = {{"INU",0x6C042},{"KAERU",0x6BE55},
        {"NEKO",0x8E0C9},{"ROBO",0x6BF8A},{"USAGI",0x67E6D}};
    return trace.status==dokodemo::startup::Status::Halted && trace.executed==5625
        && trace.pc==stops.at(source) && trace.sp==6 && trace.fp==5 && trace.diagnostic.opcode==0x93
        && trace.first_builtin.valid && trace.first_builtin.id==0xF0 && trace.first_builtin.host_service==78
        && trace.context.host_flag_104_known && trace.context.host_flag_104==0
        && trace.result.kind==dokodemo::startup::Value::Kind::StackReference && trace.result.index==12
        && trace.context.global_writes_complete && trace.context.global_writes.size()==700;
}
int SelfTest(const Options& options) {
    std::filesystem::create_directories(options.evidence);
    std::ostringstream report;
    int failures = 0, passed = 0;
    auto check = [&](bool ok, const char* name) { report << (ok ? "PASS " : "FAIL ") << name << '\n'; if (ok) ++passed; else ++failures; };
    check(Utf8(Utf8(std::string(u8"トロ / Игрок"))) == std::string(u8"トロ / Игрок"), "Japanese/Russian UTF-8 round trip");
    check(std::filesystem::is_directory(options.assets), "native asset directory exists");
    auto files = FindImages(options.assets);
    check(!files.empty(), "original decoded BMP resource exists");
    size_t decoded = 0;
    for (const auto& path : files) {
        Image image; std::wstring error;
        if (LoadBmp(path, image, error)) ++decoded;
        else { report << "FAIL BMP " << Utf8(path.wstring()) << ": " << Utf8(error) << '\n'; ++failures; }
    }
    report << "Decoded original resources: " << decoded << '\n';
    GameFont gameFont; std::wstring fontError; Image fontImage;
    check(gameFont.Load(options.assets / L"graphics/font",fontError), "original XFT loaded with recovered BOOT indexing");
    check(gameFont.RasterizePlainCp932(std::string("\x83\x67\x83\x8d"),fontImage,fontError), "original Toro glyphs rendered natively");
    check(!gameFont.RasterizePlainCp932(std::string("\x83"),fontImage,fontError), "truncated CP932 font input rejected");
    check(gameFont.RasterizePlainCp932(std::string(226,'\n'),fontImage,fontError) && fontImage.height==4084
        && !gameFont.RasterizePlainCp932(std::string(227,'\n'),fontImage,fontError)
        && !gameFont.RasterizePlainCp932(std::string(1000,'\n'),fontImage,fontError), "newline-only XFT input obeys the dimension budget");
    // Reject truncated/malicious files rather than relying on LoadImage's permissive behavior.
    auto bad = options.evidence / L"truncated.bmp";
    { std::ofstream out(bad, std::ios::binary); out << "BM"; }
    Image image; std::wstring error;
    check(!LoadBmp(bad, image, error), "truncated BMP rejected");
    std::filesystem::remove(bad);
    dokodemo::save::FoundationState state;
    state.player_name_utf8 = u8"Игрок / プレイヤー";
    state.character_key_utf8 = "NEKO";
    state.last_observed_utc_ms = dokodemo::save::ReadSystemClock().utc_ms;
    auto saved = options.evidence / L"self-test-foundation.dns";
    auto saveResult = dokodemo::save::SaveAtomic(saved, state);
    dokodemo::save::FoundationState restored;
    auto loadResult = dokodemo::save::Load(saved, restored);
    check(bool(saveResult) && bool(loadResult) && restored.player_name_utf8 == state.player_name_utf8
          && restored.last_observed_utc_ms == state.last_observed_utc_ms, "integrated atomic native save/load with Unicode");
    std::filesystem::remove(saved);
    size_t scripts = 0, commands = 0, dictionaryRecords = 0;
    for (const char* name : {"INU", "KAERU", "NEKO", "ROBO", "USAGI"}) {
        dokodemo::script::Program program; dokodemo::script::Dictionary dictionary; dokodemo::script::Diagnostic diagnostic;
        auto base = options.assets / L"scripts/original";
        bool loaded = program.load_original(base / (std::string(name) + ".KSC"), diagnostic);
        check(loaded, (std::string("original KSC verified: ") + name).c_str());
        if (loaded) {
            ++scripts; commands += program.commands().size();
            for(uint8_t hostByte : {uint8_t(0),uint8_t(1)}) {
                auto trace=dokodemo::startup::run(program,hostByte,10000);
                check(VerifiedStartupPrefix(trace,name),
                    (std::string("original startup prefix / host context ") + name + " / " + std::to_string(hostByte)).c_str());
            }
        }
        bool dicLoaded = dictionary.load_original(base / (std::string(name) + ".DIC"), diagnostic);
        check(dicLoaded, (std::string("original DIC verified: ") + name).c_str());
        if (dicLoaded) dictionaryRecords += dictionary.records().size();
    }
    report << "Original script identities: " << scripts << "; commands/metadata: " << commands << "; DIC records: " << dictionaryRecords << '\n';
    {
        dokodemo::script::Program program; dokodemo::script::Diagnostic diagnostic;
        bool loaded=program.load_original(options.assets / L"scripts/original/NEKO.KSC",diagnostic);
        if (loaded) {
            dokodemo::script::Vm vm(program);
            auto event=vm.start_at(0xC373-28,{dokodemo::script::Value::integer(10),dokodemo::script::Value::integer(14)});
            if(event.status==dokodemo::script::Status::Running) event=vm.run(1000);
            check(event.status==dokodemo::script::Status::Message && event.opcode==0x48 && event.pc==0xC359 && event.display_state==6 && event.display_count==1,
                "isolated original KSC function executes message opcode48 (fixture, no gameplay route claim)");
            check(gameFont.RasterizePlainCp932(event.message_cp932,fontImage,fontError), "executed original KSC message rendered using original XFT glyphs");
        } else check(false,"isolated original KSC function loaded");
    }
    try {
        auto modelPath = options.assets / L"models/source/NEKO.BPM/NEKO/TORO.DVS";
        std::ifstream in(modelPath,std::ios::binary);
        dokodemo::model::Bytes data((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        auto model = dokodemo::model::parseDvs(data);
        size_t vertices=0,triangles=0;
        for (const auto& node:model.nodes) if(node.hasGeometry) {
            vertices+=node.geometry.positions.size();
            if (!node.geometry.primitiveStreams.empty()) triangles+=dokodemo::model::triangles(node.geometry).size();
        }
        check(vertices==830 && triangles==1442, "original Toro DVS geometry decoded in native C++ (local space)");
    } catch (const std::exception& error) { check(false,"original Toro DVS geometry decoded in native C++"); report << error.what() << '\n'; }
    std::shared_ptr<const dokodemo::audio::PcmClip> clip; std::string audioError;
    bool audioLoaded = dokodemo::audio::LoadPcm16Wave(options.assets / L"audio/pcm/BGM_TITLE.wav",clip,audioError);
    check(audioLoaded && clip->FrameCount()==8921024 && clip->sample_rate==44100 && clip->channels==2, "original title music PCM verified in native C++");
    report << "Tests passed: " << passed << "; failures: " << failures << '\n';
    auto text = report.str(); Output(text);
    std::ofstream(options.evidence / L"self-test.txt", std::ios::binary) << text;
    return failures ? 1 : 0;
}
struct Application {
    Options options; HWND window = nullptr; HFONT font = nullptr, titleFont = nullptr;
    std::vector<std::filesystem::path> images; size_t selected = 0; Image image;
    std::vector<unsigned char> displayPixels;
    std::wstring status; uint64_t frames = 0, inputEvents = 0; bool imageLoaded = false, smokeInputSent = false;
    LARGE_INTEGER frequency{}, start{}, last{}; double elapsed = 0;
    dokodemo::save::FoundationState state;
    dokodemo::save::ClockTracker clock;
    dokodemo::script::Program program;
    std::unique_ptr<dokodemo::script::Vm> vm;
    dokodemo::audio::Win32Player player;
    GameFont gameFont;
    Image messageImage;
    bool messageVisible=false;
    bool savedSuccessfully=false, loadedSuccessfully=false, startupPrefixVerified=false;
    int width = 1080, height = 760; HDC bufferDC = nullptr; HBITMAP buffer = nullptr; HGDIOBJ oldBuffer = nullptr; void* pixels = nullptr;
    explicit Application(Options value) : options(std::move(value)) {
        images = FindImages(options.assets);
        // Prefer a prepared background/full screen to glyphs or tiny atlas members.
        for (size_t i = 0; i < images.size(); ++i) {
            auto name = images[i].wstring();
            std::transform(name.begin(), name.end(), name.begin(), towlower);
            if (name.find(L"title.bpm") != std::wstring::npos && name.find(L"alert.bmp") != std::wstring::npos) { selected = i; break; }
        }
        QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start); last = start;
        state.character_key_utf8 = "NEKO";
        font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Meiryo");
        titleFont = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        LoadSelected();
        dokodemo::script::Diagnostic diagnostic;
        if (program.load_original(options.assets / L"scripts/original/NEKO.KSC", diagnostic)) vm = std::make_unique<dokodemo::script::Vm>(program);
        std::wstring fontError; gameFont.Load(options.assets / L"graphics/font",fontError);
    }
    ~Application() { ReleaseBuffer(); DeleteObject(font); DeleteObject(titleFont); }
    void ReleaseBuffer() {
        if (bufferDC && oldBuffer) SelectObject(bufferDC, oldBuffer);
        if (buffer) DeleteObject(buffer);
        if (bufferDC) DeleteDC(bufferDC);
        bufferDC = nullptr; buffer = nullptr; oldBuffer = nullptr; pixels = nullptr;
    }
    void LoadSelected() {
        messageVisible=false;
        imageLoaded = false;
        if (images.empty()) { status = L"Данные не подготовлены. Выполните scripts/bootstrap-assets.ps1."; return; }
        std::wstring error;
        imageLoaded = LoadBmp(images[selected], image, error);
        if(imageLoaded) {
            displayPixels=image.bgra;
            // Explicit black diagnostic matte for straight-alpha source pixels.
            for(size_t p=0;p<displayPixels.size();p+=4) {
                for(int c=0;c<3;++c) displayPixels[p+c]=static_cast<unsigned char>(unsigned(displayPixels[p+c])*displayPixels[p+3]/255);
                displayPixels[p+3]=255;
            }
        }
        status = imageLoaded ? L"Оригинальный ресурс декодирован; сценарная сцена ещё не восстановлена." : error;
    }
    void Text(HDC dc, const std::wstring& text, RECT rect, COLORREF color, UINT flags = DT_LEFT | DT_TOP | DT_WORDBREAK) {
        SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT); DrawTextW(dc, text.c_str(), -1, &rect, flags);
    }
    void Fill(HDC dc, RECT rect, COLORREF color) { auto brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush); }
    void Render(HDC dc) {
        Fill(dc, {0,0,width,height}, RGB(18,23,30));
        SelectObject(dc, titleFont);
        Text(dc, L"Doko Demo Issyo · native recovery", {24,18,width-24,58}, RGB(241,244,246));
        SelectObject(dc, font);
        Text(dc, L"Промежуточная сборка восстановления. Полная игра пока не реализована.", {24,58,width-24,88}, RGB(255,198,109));
        int top = 104, bottom = std::max(top + 100, height - 178);
        int availableW = width - 48, availableH = bottom - top;
        double scale = std::min(double(availableW) / 480.0, double(availableH) / 272.0);
        int vw = int(480 * scale), vh = int(272 * scale), vx = (width-vw)/2, vy = top+(availableH-vh)/2;
        Fill(dc, {vx,vy,vx+vw,vy+vh}, RGB(0,0,0));
        if (messageVisible) {
            // Isolated bytecode fixture on a neutral surface, with original XFT
            // pixels. It is never attached to an invented gameplay background.
            auto raster=messageImage.bgra;
            for(size_t p=0;p<raster.size();p+=4) {
                for(int c=0;c<3;++c) raster[p+c]=static_cast<unsigned char>(unsigned(raster[p+c])*raster[p+3]/255);
                raster[p+3]=255;
            }
            BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=messageImage.width;
            info.bmiHeader.biHeight=-messageImage.height; info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            int drawScale=std::max(1,int(scale*2));
            StretchDIBits(dc,vx+int(16*scale),vy+int(80*scale),messageImage.width*drawScale,messageImage.height*drawScale,
                0,0,messageImage.width,messageImage.height,raster.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        } else if (imageLoaded) {
            // Preserve resource aspect ratio. A texture does not become an invented game scene.
            double imageScale = std::min(double(vw)/image.width, double(vh)/image.height);
            int iw = int(image.width*imageScale), ih = int(image.height*imageScale);
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = image.width;
            info.bmiHeader.biHeight = -image.height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, vx+(vw-iw)/2, vy+(vh-ih)/2, iw, ih, 0,0,image.width,image.height,displayPixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        } else Text(dc, status, {vx+20,vy+20,vx+vw-20,vy+vh-20}, RGB(239,239,239));
        int y = bottom + 16;
        auto source = messageVisible ? L"NEKO.KSC · файл 0xC373 · изолированная функция · аргументы 10, 14" : imageLoaded ? std::filesystem::relative(image.path, options.assets).wstring() : L"нет ресурса";
        Text(dc, L"Исходный ресурс: " + source, {24,y,width-24,y+28}, RGB(181,205,221), DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_SINGLELINE);
        Text(dc, status, {24,y+30,width-24,y+78}, RGB(211,219,223));
        Text(dc, L"←/→ ресурсы   F1/F4 KSC   F2 сообщение   F3 музыка   F5/F9 save/load   F12 снимок", {24,height-62,width-24,height-32}, RGB(167,181,192));
        std::wostringstream metrics; metrics << L"Win32 · " << frames << L" кадров · " << inputEvents << L" событий ввода · " << images.size() << L" ресурсов";
        Text(dc, metrics.str(), {24,height-31,width-24,height-8}, RGB(125,149,163));
    }
    void EnsureBuffer() {
        if (bufferDC) return;
        auto dc = GetDC(window); bufferDC = CreateCompatibleDC(dc);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        buffer = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0); ReleaseDC(window, dc);
        if (!buffer || !bufferDC) throw std::runtime_error("Cannot create native rendering buffer");
        oldBuffer = SelectObject(bufferDC, buffer);
    }
    void Paint() { EnsureBuffer(); Render(bufferDC); PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint); BitBlt(dc,0,0,width,height,bufferDC,0,0,SRCCOPY); EndPaint(window,&paint); }
    void Capture() {
        EnsureBuffer(); Render(bufferDC); std::wstring error;
        if (WriteBmp(options.evidence / L"native-window.bmp", width,height,pixels,error)) status = L"Снимок записан в " + options.evidence.wstring();
        else status = error;
    }
    void Audio() {
        if(player.IsPlaying()) {player.Stop();status=L"Нативное воспроизведение остановлено.";return;}
        const auto* catalog=dokodemo::audio::FindOriginalClip("BGM_TITLE");
        if(!catalog) {status=L"Исходный BGM_TITLE не проиндексирован.";return;}
        auto path=options.assets / std::filesystem::path(catalog->path).lexically_relative(L"assets/native");
        std::shared_ptr<const dokodemo::audio::PcmClip> clip; std::string error;
        if(!dokodemo::audio::LoadPcm16Wave(path,clip,error)) {status=Utf8(error);return;}
        if(clip->FrameCount()!=catalog->frames || clip->sample_rate!=catalog->rate || clip->channels!=catalog->channels) {status=L"PCM не совпадает с оригинальным аудиокаталогом.";return;}
        std::optional<dokodemo::audio::SampleLoop> loop;
        if(catalog->loop_begin>=0 && catalog->loop_end>catalog->loop_begin) loop=dokodemo::audio::SampleLoop{static_cast<uint64_t>(catalog->loop_begin),static_cast<uint64_t>(catalog->loop_end)};
        bool ok=player.Play(clip,loop,error);
        status=ok ? L"Оригинальный BGM_TITLE: нативный WinMM, intro + исходный sample loop. F3 остановит звук." : Utf8(error);
    }
    void Key(WPARAM key) {
        ++inputEvents;
        if ((key == VK_RIGHT || key == VK_LEFT) && !images.empty()) {
            selected = key == VK_RIGHT ? (selected+1)%images.size() : (selected+images.size()-1)%images.size(); LoadSelected();
        } else if (key == VK_F1) RunScript();
        else if (key == VK_F2) MessageFixture();
        else if (key == VK_F3) Audio();
        else if (key == VK_F4) StartupProbe();
        else if (key == VK_F5) Save();
        else if (key == VK_F9) Load();
        else if (key == VK_F12) Capture();
        else if (key == VK_ESCAPE) DestroyWindow(window);
        InvalidateRect(window,nullptr,FALSE);
    }
    void StartupProbe() {
        messageVisible=false;
        if(!program.loaded()) {status=L"NEKO.KSC не загружен.";return;}
        // Explicit probe context, not a guessed value for a complete game start.
        auto trace=dokodemo::startup::run(program,0,10000);
        startupPrefixVerified=VerifiedStartupPrefix(trace,"NEKO");
        status=L"Проба исходного старта (host byte = 0): " + std::to_wstring(trace.executed) + L" команд; " + Utf8(trace.diagnostic.text()) + L". Реальное начальное значение host-поля ещё не доказано.";
        std::filesystem::create_directories(options.evidence);
        std::ofstream report(options.evidence / L"startup-prefix.json",std::ios::binary);
        report << "{\"probe\":true,\"host_byte\":0,\"executed\":" << trace.executed << ",\"pc\":" << trace.pc << ",\"sp\":" << trace.sp << ",\"fp\":" << trace.fp
               << ",\"opcode\":" << trace.diagnostic.opcode << ",\"first_host_service\":" << trace.first_builtin.host_service
               << ",\"global_writes\":" << trace.context.global_writes.size() << ",\"result_stack_reference\":" << trace.result.index
               << ",\"host_initialization_observed\":false,\"full_game\":false}\n";
    }
    void RunScript() {
        messageVisible=false;
        if (!vm) { status = L"Оригинальный NEKO.KSC отсутствует или не прошёл проверку."; return; }
        // The original caller's input slots are unknown; no invented values are supplied.
        auto event = vm->start();
        if (event.status == dokodemo::script::Status::Running) event = vm->run(1000);
        if (event.status == dokodemo::script::Status::Stopped) status = L"Исходный старт остановлен: " + Utf8(event.diagnostic.text());
        else if (event.status == dokodemo::script::Status::Message) status = L"Подтверждённая команда сообщения; графический контекст сцены ещё не восстановлен.";
        else status = L"KSC достиг лимита исполнения; продолжение требует проверки контекста.";
        std::filesystem::create_directories(options.evidence);
        std::ofstream(options.evidence / L"startup-diagnostic.txt", std::ios::binary) << Utf8(status) << '\n';
    }
    void MessageFixture() {
        messageVisible=false;
        if(!vm) {status=L"NEKO.KSC не загружен.";return;}
        auto event=vm->start_at(0xC373-28,{dokodemo::script::Value::integer(10),dokodemo::script::Value::integer(14)});
        if(event.status==dokodemo::script::Status::Running) event=vm->run(1000);
        std::wstring error;
        if(event.status==dokodemo::script::Status::Message && gameFont.RasterizePlainCp932(event.message_cp932,messageImage,error)) {
            messageVisible=true;
            status=L"Выполнена исходная opcode 0x48, PC 0xC359, display state 6. Изолированная проверка; маршрут игры и тайминг не восстановлены.";
            std::filesystem::create_directories(options.evidence);
            std::ofstream report(options.evidence / L"message-fixture.json",std::ios::binary);
            report << "{\"source_sha256\":" << JsonString(program.source().sha256) << ",\"fixture\":true,\"gameplay_route_verified\":false,\"pc\":"
                   << event.pc << ",\"opcode\":" << event.opcode << ",\"display_state\":" << event.display_state << ",\"original_xft\":true}\n";
        } else status=event.status==dokodemo::script::Status::Stopped ? Utf8(event.diagnostic.text()) : error;
    }
    std::filesystem::path SavePath() { return options.evidence / L"saves/recovery-state.dns"; }
    void Save() {
        std::filesystem::create_directories(SavePath().parent_path());
        auto result = dokodemo::save::SaveAtomic(SavePath(), state);
        savedSuccessfully=bool(result);
        status = result ? L"Состояние нативного стенда сохранено (собственный формат, не PSP savedata)." : Utf8(result.message);
    }
    void Load() {
        auto result = dokodemo::save::Load(SavePath(), state);
        loadedSuccessfully=bool(result);
        if (result) { clock.Reset(); status = L"Состояние нативного стенда загружено. Игровая семантика времени ещё не восстановлена."; }
        else status = Utf8(result.message);
    }
    void Tick() {
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        elapsed = double(now.QuadPart-start.QuadPart)/frequency.QuadPart;
        auto reading = dokodemo::save::ReadSystemClock();
        auto observation = clock.Observe(reading);
        state.observed_session_ms += observation.elapsed_monotonic_ms;
        state.last_observed_utc_ms = reading.utc_ms;
        state.last_observed_utc_offset_minutes = reading.utc_offset_minutes;
        if (double(now.QuadPart-last.QuadPart)/frequency.QuadPart >= 1.0/60.0) {
            ++frames; last = now; InvalidateRect(window,nullptr,FALSE);
        }
        if (options.smoke && frames >= 30 && !smokeInputSent) {
            smokeInputSent=true;
            for(auto key : {VK_RIGHT,VK_LEFT,VK_F5,VK_F9,VK_F1,VK_F4,VK_F2}) SendMessageW(window,WM_KEYDOWN,key,0);
        }
        if (options.smoke && elapsed > 2.0) {
            Capture();
            std::ofstream report(options.evidence / L"smoke-test.json", std::ios::binary);
            report << "{\n  \"native_win32_loop\": true,\n  \"frames\": " << frames << ",\n  \"input_events\": " << inputEvents
                << ",\n  \"image_loaded\": " << (imageLoaded ? "true" : "false") << ",\n  \"asset_count\": " << images.size()
                << ",\n  \"input_method\": \"WM_KEYDOWN\",\n  \"native_save_ok\": " << (savedSuccessfully ? "true" : "false")
                << ",\n  \"native_load_ok\": " << (loadedSuccessfully ? "true" : "false")
                << ",\n  \"startup_prefix_verified\": " << (startupPrefixVerified ? "true" : "false")
                << ",\n  \"original_message_rendered\": " << (messageVisible ? "true" : "false")
                << ",\n  \"full_game\": false,\n  \"source\": " << JsonString(messageVisible ? "scripts/original/NEKO.KSC" : Utf8(image.path.wstring())) << "\n}\n";
            DestroyWindow(window);
        }
    }
};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { app = reinterpret_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); app->window = window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
    if (!app) return DefWindowProcW(window,message,wParam,lParam);
    switch (message) {
        case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {640,480}; return 0;
        case WM_SIZE: app->width = std::max(1,int(LOWORD(lParam))); app->height = std::max(1,int(HIWORD(lParam))); app->ReleaseBuffer(); return 0;
        case WM_PAINT: app->Paint(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_KEYDOWN: app->Key(wParam); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window,message,wParam,lParam);
    }
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        SetProcessDPIAware(); auto options = ParseOptions();
        if (options.selfTest) return SelfTest(options);
        Application app(options);
        WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.lpszClassName = L"DokodemoNativeRecovery"; wc.hCursor = LoadCursorW(nullptr,IDC_ARROW);
        if (!RegisterClassW(&wc)) throw std::runtime_error("Cannot register Win32 window");
        auto window = CreateWindowExW(0,wc.lpszClassName,L"Doko Demo Issyo — native recovery (incomplete)",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1100,800,nullptr,nullptr,instance,&app);
        if (!window) throw std::runtime_error("Cannot create Win32 window");
        ShowWindow(window, show); UpdateWindow(window);
        MSG message{}; bool running = true;
        while (running) {
            while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if (message.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            if (running) { app.Tick(); MsgWaitForMultipleObjects(0,nullptr,FALSE,2,QS_ALLINPUT); }
        }
        return options.smoke && (!app.imageLoaded || app.frames < 30 || app.inputEvents < 7 || !app.savedSuccessfully || !app.loadedSuccessfully
            || !app.startupPrefixVerified || !app.messageVisible) ? 1 : 0;
    } catch (const std::exception& error) { Output(std::string("ERROR: ")+error.what()+"\n"); return 2; }
}
