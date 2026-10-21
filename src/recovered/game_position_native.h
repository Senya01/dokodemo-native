#pragma once
#include "game_model_native.h"

namespace dokodemo::recovered {
struct GameRegionInput {
    uint32_t node=0, selector=0, shape=0;
    float first=0, second=0;
    GameMatrix world=ModelIdentity();
};
struct GameRegionRecord {
    uint32_t node=0, shape=0;
    float radius=0;
    GameVector4 center{};
    std::array<GameVector4,4> corners{};
    std::array<std::array<float,3>,4> planes{};
};
// 26B30/26B80: three independently owned compiled region lists. Type12's
// reached selector/shape/dimensions are data, not particle-emission behavior.
// Matrices use the already named native canonical world-basis adaptation.
class GamePositionRegions final {
public:
    GameFsStatus Build(const GameModelClone& model);
    GameFsStatus Initialize(const std::vector<GameRegionInput>& nodes);
    GameFsStatus ContainsSecond(GameVector4 point,bool& result) const;
    GameFsStatus FallbackSecond(GameVector4 point,GameVector4& result) const;
    const std::array<std::vector<GameRegionRecord>,3>& Lists() const { return lists_; }
    bool Live() const { return live_; }
    void Close() { lists_={};live_=false; }
private:
    std::array<std::vector<GameRegionRecord>,3> lists_;
    bool live_=false;
};
} // namespace dokodemo::recovered
