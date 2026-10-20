#include "recovered/game_graphics_native.h"
#include "recovered/game_boot_native.h"
#include "recovered/rng_native.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace dokodemo::recovered;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
uint32_t At(const GameGraphicsFrame& frame, uint32_t x, uint32_t y) { return frame.pixels.at(y * frame.stride + x); }
void SurfaceTests() {
    GameGraphics graphics;
    auto config = GameGraphicsConfig{};
    config.width = config.height = 16; config.stride = 32;
    config.scissor_left = config.scissor_top = 2;
    config.scissor_right = config.scissor_bottom = 14;
    Check(graphics.Initialize(config), "create two native color surfaces and depth");
    auto initial = graphics.Snapshot();
    Check(initial.live && initial.color_bytes == 4096 && initial.depth_bytes == 1024, "actual stride allocation");
    GameGraphicsFrame absent;
    Check(!graphics.CopyFront(absent) && !graphics.RecordPresented(initial.generation, 1), "unsubmitted data is not a presented frame");
    Check(graphics.Clear(0xFF000000, 0), "clear buffers");
    Check(graphics.FillRectangle(0, 0, 16, 16, 0xFFFF0000, 100), "clipped opaque primitive");
    Check(graphics.FillRectangle(3, 3, 8, 8, 0xFF0000FF, 50), "behind primitive");
    Check(graphics.FillRectangle(4, 4, 8, 8, 0x0000FF00, 200), "alpha-zero primitive");
    Check(graphics.FillRectangle(6, 6, 8, 8, 0x800000FF, 200), "front alpha primitive");
    Check(graphics.SwapBuffers(), "publish complete frame");
    Check(graphics.Snapshot().display_buffer_index == 0 && graphics.Snapshot().draw_buffer_index == 1,
          "source initial swap selects separate displayed and next draw owners");
    GameGraphicsFrame first;
    Check(graphics.CopyFront(first) && At(first, 0, 0) == 0xFF000000 && At(first, 2, 2) == 0xFFFF0000, "scissor edges");
    Check(At(first, 3, 3) == 0xFFFF0000 && At(first, 4, 4) == 0xFFFF0000, "depth and alpha tests affect real pixels");
    Check(At(first, 6, 6) == 0xFF7F0080, "source-alpha blend result");
    Check(NativeColorFromPspAbgr(0x80402010) == 0x80102040, "PSP ABGR to native DIB channels");
    Check(graphics.Snapshot().present_count == 0, "copy is not presentation");
    Check(graphics.RecordPresented(first.generation, first.sequence), "actual frontend presentation acknowledged");
    Check(graphics.Clear(0xFF112233, 0) && graphics.SwapBuffers(), "second independent back buffer");
    GameGraphicsFrame second;
    Check(graphics.CopyFront(second) && At(second, 6, 6) == 0xFF112233 && At(first, 6, 6) == 0xFF7F0080, "frame copy ownership survives swaps");
    Check(!graphics.Initialize(config) && graphics.Snapshot().frame_sequence == 2, "live reinit refused without destroying surfaces");
    graphics.Close();
    Check(!graphics.Snapshot().live && !graphics.Snapshot().color_bytes && !graphics.Snapshot().depth_bytes,
          "close releases actual buffers");
    Check(!graphics.CopyFront(second) && !graphics.RecordPresented(first.generation, first.sequence), "closed owner rejects stale operations");
    auto invalid = config; invalid.stride = 1;
    Check(!graphics.Initialize(invalid), "invalid layout refused");
    Check(graphics.Initialize(config) && graphics.Snapshot().generation != first.generation
        && !graphics.RecordPresented(first.generation, 1), "generation prevents old frontend acknowledgement");
}
void TriangleTests() {
    GameGraphics graphics;
    GameGraphicsConfig config; config.width = config.height = config.stride = 16;
    config.scissor_right = config.scissor_bottom = 16;
    Check(graphics.Initialize(config), "triangle surface");
    Check(graphics.Clear(0xFF000000), "triangle clear");
    const std::array<GameVertex, 3> triangle{{{2, 2, 10, 0xFFFF0000}, {2, 14, 10, 0xFF0000FF}, {14, 2, 10, 0xFF00FF00}}};
    Check(graphics.DrawTriangle(triangle) && graphics.SwapBuffers(), "rasterize triangle");
    GameGraphicsFrame frame;
    Check(graphics.CopyFront(frame) && At(frame, 3, 3) != 0xFF000000 && At(frame, 14, 14) == 0xFF000000, "triangle coverage is not its bounding box");
    auto invalid = triangle; invalid[0].x = std::numeric_limits<float>::quiet_NaN();
    Check(!graphics.DrawTriangle(invalid), "nonfinite vertex refused before raster work");
    invalid = triangle; invalid[2] = invalid[1];
    Check(!graphics.DrawTriangle(invalid), "degenerate triangle refused");
    Check(graphics.Clear(0xFF112233), "back-face clear");
    auto back = triangle; std::swap(back[1], back[2]);
    Check(graphics.DrawTriangle(back) && graphics.SwapBuffers() && graphics.CopyFront(frame)
        && At(frame, 3, 3) == 0xFF112233, "source cull/front-face state applied");
}
void ViewportTests() {
    GameGraphics graphics;
    Check(graphics.Initialize({}), "source C0C surface");
    GameVertex a, b;
    Check(graphics.ProjectClipVertex({-1, 1, -1, 0xFFFFFFFF}, a)
        && graphics.ProjectClipVertex({1, -1, 1, 0xFFFFFFFF}, b), "viewport transform");
    Check(a.x == 0 && a.y == 0 && a.depth == 65500 && b.x == 480 && b.y == 272 && b.depth == 100,
          "source viewport/offset/depth range applied to native coordinates");
    Check(!graphics.ProjectClipVertex({0, 0, 2, 0}, a), "unclipped depth refused");
}
void StartupTimeTests() {
    std::array<uint8_t, 16> local{{0xE8, 7, 2, 0, 29, 0, 23, 0, 58, 0, 57, 0, 0, 0, 0, 0}};
    Check(GameCalendarDE0(local) == std::array<uint32_t, 7>{{0, 57, 58, 23, 29, 2, 2024}}, "DE0 field order from already-local date");
    RngNativeOwner rng;
    Check(!rng.Available(), "no default invented RNG seed");
    rng.ApplyInitializerTimeT32(1709265537, 1, "new startup caller test's explicit local civil time");
    const auto sample = rng.Snapshot();
    Check(sample.seed_available && sample.seed == 1709265537 && sample.origin == RngSeedOrigin::InitializerTimeT32,
          "C321C keeps real startup time word without another timezone shift");
}
void MatrixTests() {
    GameGraphics graphics;
    Check(graphics.Initialize({}), "matrix workspace allocation");
    const auto state = graphics.Snapshot();
    Check(state.matrix_mode == 2 && state.matrix_bytes == 48 * 64
        && state.matrix_depths == std::array<uint32_t, 4>{{1, 1, 1, 0}}, "C0C workspace capacities and selected model mode");
    GameMatrix matrix;
    Check(graphics.ReadMatrix(2, matrix) && matrix[0] == 1 && matrix[5] == 1 && matrix[10] == 1 && matrix[15] == 1, "initial identity");
    Check(graphics.PushMatrix(), "owned matrix push");
    matrix[12] = 7;
    Check(graphics.LoadMatrix(matrix) && graphics.PopMatrix() && graphics.ReadMatrix(2, matrix) && matrix[12] == 0,
          "matrix stack copies do not alias parent");
    Check(!graphics.PopMatrix() && !graphics.SetMatrixMode(3), "root underflow and unallocated stack refused");
    for (unsigned i = 1; i < 32; ++i) Check(graphics.PushMatrix(), "model stack capacity");
    Check(!graphics.PushMatrix(), "matrix stack overflow bounded");
    graphics.Close();
    Check(!graphics.Snapshot().matrix_bytes && !graphics.ReadMatrix(2, matrix), "matrix lifetime closes with renderer");
}
void RuntimeResetTests() {
    GameRuntimeResetState state;
    state.primary.fill(0xA5); state.secondary.fill(0x5A);
    ResetGameRuntime253B8(state);
    for (const auto byte : state.primary) Check(byte == 0, "253B8 clears every primary byte");
    for (const auto byte : state.secondary) Check(byte == 0, "258D4 clears every secondary byte");
    // Reached reset replaces previous live state on a later invocation too.
    state.primary.back() = 1; state.secondary.front() = 2;
    ResetGameRuntime253B8(state);
    Check(state.primary.back() == 0 && state.secondary.front() == 0, "explicit reset is repeatable");
}
}
int main() {
    try { SurfaceTests(); TriangleTests(); ViewportTests(); MatrixTests(); StartupTimeTests(); RuntimeResetTests();
        std::cout << "game graphics: surfaces, state, raster, ownership, startup time and reset PASS\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
