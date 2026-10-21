#include "game_storage_win32.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace dokodemo;
using namespace dokodemo::recovered;
namespace {
void Check(bool value, const char *text) {
    if (!value)
        throw std::runtime_error(text);
}
struct SyntheticStorage : GameStorage {
    GameStorageSnapshot state;
    GameStorageStatus result{GameStorageCode::Missing, 0, "synthetic missing"};
    GameStorageStatus Probe() override {
        state.available = true;
        ++state.probe_calls;
        return {};
    }
    GameStorageStatus LoadNative(std::vector<uint8_t> &out) override {
        ++state.load_calls;
        out = {1, 2};
        return result;
    }
    GameStorageStatus PublishNative(const std::vector<uint8_t> &, bool) override {
        throw std::runtime_error("autoload must not publish");
    }
    GameStorageSnapshot Snapshot() const override {
        return state;
    }
};
void FlowTests() {
    SyntheticStorage source;
    source.state.free_bytes = 800 * 1024;
    GameAutoload flow;
    flow.Begin(true);
    Check(!flow.DialogCompleted(1) && !flow.DialogCompleted(2), "no forced dialog completion");
    Check(flow.DialogCompleted(3), "natural closed dialog advances");
    flow.Tick(source);
    auto s = flow.Snapshot();
    Check(s.phase == GameAutoloadPhase::TitleRequested && s.mode == 0 && s.parameter_bytes == 0x5C8 &&
              s.buffer_capacity == 0x48000 && s.buffer_live && !s.source_fields_applied,
          "missing+actual threshold selects TITLE,not imported save");
    flow.Close();
    Check(!flow.Snapshot().buffer_live, "autoload buffer retires");
    source.state.free_bytes = 800 * 1024 - 1;
    flow.Begin(false);
    flow.Tick(source);
    Check(flow.Snapshot().phase == GameAutoloadPhase::Blocked &&
              flow.Snapshot().status.code == GameStorageCode::Full,
          "low-space not title success");
    source.result = {};
    flow.Begin(false);
    flow.Tick(source);
    Check(flow.Snapshot().phase == GameAutoloadPhase::Blocked && !flow.Snapshot().source_fields_applied
        &&flow.Snapshot().status.code==GameStorageCode::UnmappedNativeState,
          "opaque positive native envelope is not original state import");
    for (auto code : {GameStorageCode::Unavailable, GameStorageCode::ReadOnly, GameStorageCode::Corrupt,
                      GameStorageCode::IoFailure}) {
        source.result = {code, 123, "synthetic error"};
        flow.Begin(false);
        flow.Tick(source);
        Check(flow.Snapshot().status.code == code && flow.Snapshot().phase == GameAutoloadPhase::Blocked,
              "storage error preserved");
    }
}
} // namespace
int main() {
    std::filesystem::path root;
    try {
        FlowTests();
        wchar_t temp[32768]{};
        Check(GetTempPathW(32768, temp) > 0, "test temp path");
        root = std::filesystem::path(temp) /
               (L"dokodemo-storage-tests-" + std::to_wstring(GetCurrentProcessId()));
        Check(!std::filesystem::exists(root), "fresh isolated test root");
        WindowsGameStorageOptions options;
        options.root = root.wstring();
        auto storage = CreateWindowsGameStorage(options);
        Check(bool(storage->Probe()) && storage->Snapshot().free_bytes > 800 * 1024,
              "actual directory/free space");
        std::vector<uint8_t> loaded;
        Check(storage->LoadNative(loaded).code == GameStorageCode::Missing, "actual first run missing");
        const std::vector<uint8_t> payload{0, 1, 255, 7, 0, 32};
        Check(bool(storage->PublishNative(payload, false)), "actual completed-write publication");
        storage = CreateWindowsGameStorage(options);
        Check(bool(storage->LoadNative(loaded)) && loaded == payload,
              "fresh owner persists/reads identical native payload");
        Check(storage->PublishNative({9}, false).code == GameStorageCode::Exists,
              "existing save preserved without replacement authority");
        Check(bool(storage->LoadNative(loaded)) && loaded == payload, "failed publication leaves old bytes");
        Check(bool(storage->PublishNative({8, 9}, true)) && bool(storage->LoadNative(loaded)) &&
                  loaded == std::vector<uint8_t>({8, 9}),
              "explicit replacement/readback");
        options.read_only = true;
        auto readonly = CreateWindowsGameStorage(options);
        Check(bool(readonly->LoadNative(loaded)) &&
                  readonly->PublishNative(payload, true).code == GameStorageCode::ReadOnly,
              "read-only can load but not publish");
        options.read_only = false;
        const auto slot = root / L"UCJS10002" / L"000";
        {
            std::ofstream corrupt(slot / L"DATA.NONMATCHING.V1", std::ios::binary | std::ios::trunc);
            corrupt << "bad";
        }
        Check(storage->LoadNative(loaded).code == GameStorageCode::Corrupt,
              "actual truncated native file rejected");
        auto envelope = EncodeNativeSave(payload);
        envelope.back() ^= 1;
        Check(DecodeNativeSave(envelope, loaded).code == GameStorageCode::Corrupt,
              "checksum rejects changed payload");
        {
            std::ofstream original(slot / L"DATA.BIN", std::ios::binary);
            original << "original untouched";
        }
        Check(storage->LoadNative(loaded).code == GameStorageCode::UnsupportedOriginal &&
                  storage->PublishNative(payload, true).code == GameStorageCode::UnsupportedOriginal &&
                  std::filesystem::file_size(slot / L"DATA.BIN") == 18,
              "original PSP file preserved,not interpreted");
        options.unavailable = true;
        Check(CreateWindowsGameStorage(options)->Probe().code == GameStorageCode::Unavailable,
              "explicit unavailable policy");
        options.unavailable = false;
        options.forbidden_roots = {root.wstring()};
        Check(CreateWindowsGameStorage(options)->Probe().code == GameStorageCode::ReadOnly,
              "assets root exclusion");
        storage.reset();
        readonly.reset();
        std::filesystem::remove_all(root);
        std::cout << "native storage: actual persistence/errors/publication,opaque PSP boundary,autoload "
                     "threshold/dialog ordering PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
