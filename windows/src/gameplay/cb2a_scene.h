#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dokodemo::gameplay {
struct Cb2aMedia {
    const char* clip; std::size_t clip_size; const char* clip_hash;
    const char* bank; std::size_t bank_size; const char* bank_hash;
};
struct Cb2aClip { unsigned clip; int frame; bool relative=false; };
struct Cb2aPlacement { float x=0, y=0, sx=1, sy=1; };
struct Cb2aNode {
    unsigned node;
    std::optional<int> sprite;
    std::optional<float> x,y,sx,sy;
};
using Cb2aCallback=std::function<void(unsigned,int,int)>;
struct Cb2aSceneData;

// The original resource binds a base animation and samples controls over it.
// Screen placement is relative to the 480x272 viewport center.
class Cb2aScene {
public:
    bool Load(const std::filesystem::path& directory,const Cb2aMedia& media,std::string& error);
    bool Draw(unsigned root,const std::vector<Cb2aClip>& clips,std::vector<std::uint8_t>& vram,
              std::size_t offset,const Cb2aPlacement& transform={},const Cb2aCallback& callback={},
              const std::vector<Cb2aNode>& overrides={}) const;
private:
    std::shared_ptr<const Cb2aSceneData> scene_;
};
}
