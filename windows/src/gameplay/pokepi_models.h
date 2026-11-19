#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace dokodemo::gameplay {
class PokepiModels {
  public:
    bool Load(const std::filesystem::path &title_directory, std::string &error);
    unsigned Duration(unsigned type, unsigned motion) const;
    bool Draw(unsigned type, unsigned motion, std::uint32_t source_time, float x, float y, float z,
              unsigned panel, std::vector<std::uint8_t> &vram, std::size_t draw_offset) const;

  private:
    struct Data;
    std::shared_ptr<const Data> data_;
};
} // namespace dokodemo::gameplay
