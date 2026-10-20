#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

namespace dokodemo::recovered {
// Direct C++ rendering API, not an interpreter for PSP GE command lists.
// Pixels use native ARGB words (BGRA bytes for Windows DIB presentation).
constexpr uint32_t NativeColorFromPspAbgr(uint32_t color) noexcept {
    return (color & 0xFF00FF00u) | ((color & 0xFFu) << 16) | ((color >> 16) & 0xFFu);
}
struct GameGraphicsConfig {
    uint32_t width = 480, height = 272, stride = 512;
    uint32_t source_pixel_format = 3;
    std::array<uint32_t, 3> source_buffer_offsets{{0, 0x88000, 0x110000}};
    int32_t offset_x = 1808, offset_y = 1912;
    int32_t viewport_x = 2048, viewport_y = 2048;
    uint16_t depth_near = 65500, depth_far = 100;
    uint32_t scissor_left = 0, scissor_top = 0, scissor_right = 480, scissor_bottom = 272;
    bool scissor = true, alpha_test = true, depth_test = true, blend = true;
    bool depth_write = true;
    bool cull_face = true, front_face_counter_clockwise = true, smooth_shading = true;
    uint32_t enabled_state_mask = 0x137;
    uint32_t ambient_psp_abgr = 0xFF404040, color_material = 3;
    std::array<uint32_t, 4> matrix_capacities{{8, 8, 32, 0}};
    uint32_t initial_matrix_mode = 2;
};
struct GameGraphicsSnapshot {
    GameGraphicsConfig config;
    bool initialized = false, live = false, diagnostic_frame = false;
    uint64_t generation = 0, frame_sequence = 0, present_count = 0;
    uint64_t last_presented_sequence = 0, draw_calls = 0;
    uint64_t color_bytes = 0, depth_bytes = 0;
    uint32_t draw_buffer_index = 0, display_buffer_index = 1;
    uint32_t matrix_mode = 2;
    std::array<uint32_t, 4> matrix_depths{};
    uint64_t matrix_bytes = 0;
};
struct GameGraphicsFrame {
    uint32_t width = 0, height = 0, stride = 0;
    uint64_t generation = 0, sequence = 0;
    bool diagnostic = false;
    std::vector<uint32_t> pixels;
};
struct GameVertex {
    float x = 0, y = 0;
    uint16_t depth = 0;
    uint32_t color = 0xFFFFFFFF;
};
struct GameClipVertex { float x = 0, y = 0, z = 0; uint32_t color = 0xFFFFFFFF; };
using GameMatrix = std::array<float, 16>; // native column-major math
class GameGraphics final {
public:
    bool Initialize(const GameGraphicsConfig& config);
    bool Clear(uint32_t native_argb, uint16_t depth = 0);
    bool FillRectangle(int32_t left, int32_t top, int32_t right, int32_t bottom,
                       uint32_t native_argb, uint16_t depth = 0);
    bool DrawTriangle(const std::array<GameVertex, 3>& vertices);
    bool ProjectClipVertex(const GameClipVertex& input, GameVertex& output) const;
    bool SetMatrixMode(uint32_t mode);
    bool LoadMatrix(const GameMatrix& matrix);
    bool PushMatrix();
    bool PopMatrix();
    bool ReadMatrix(uint32_t mode, GameMatrix& output) const;
    bool SwapBuffers();
    bool CopyFront(GameGraphicsFrame& out) const;
    // Called only after a successful actual Windows presentation, not copying
    // or capturing an offscreen framebuffer.
    bool RecordPresented(uint64_t generation, uint64_t sequence);
    bool DrawDiagnosticFrame(); // explicit test pattern, never game/title content
    GameGraphicsSnapshot Snapshot() const;
    void Close() noexcept;
private:
    bool Inside(int32_t x, int32_t y) const noexcept;
    void Pixel(uint32_t x, uint32_t y, uint32_t argb, uint16_t depth) noexcept;
    mutable std::mutex mutex_;
    GameGraphicsSnapshot state_;
    std::array<std::vector<uint32_t>, 2> color_;
    std::vector<uint16_t> depth_;
    std::vector<GameMatrix> matrices_;
    std::array<uint32_t, 4> matrix_offsets_{};
    uint32_t front_ = 1, back_ = 0;
};
} // namespace dokodemo::recovered
