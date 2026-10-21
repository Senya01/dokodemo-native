#include "game_position_native.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void Need(bool value,const char* text) { if(!value)throw std::runtime_error(text); }
void Finite(const GameVector4& point) { for(auto value:point)Need(std::isfinite(value),"Nonfinite region point"); }
double Distance(const GameVector4& a,const GameVector4& b) {
    return std::hypot(double(a[0])-b[0],double(a[1])-b[1]);
}
bool Contains(const GameRegionRecord& region,const GameVector4& point) {
    if(region.shape==2)return Distance(region.center,point)<=region.radius;
    for(const auto& plane:region.planes)
        if(double(plane[0])*point[0]+double(plane[1])*point[1]+plane[2]<0)return false;
    return true;
}
bool FullyContained(const GameRegionRecord& candidate,const GameRegionRecord& blocker) {
    if(candidate.shape==1) {
        for(const auto& corner:candidate.corners) {
            if(blocker.shape==2){if(Distance(blocker.center,corner)>=blocker.radius)return false;}
            else if(!Contains(blocker,corner))return false;
        }
        return true;
    }
    if(blocker.shape==2)return Distance(candidate.center,blocker.center)+candidate.radius<blocker.radius;
    if(!Contains(blocker,candidate.center))return false;
    for(size_t i=0;i<4;++i) {
        const auto& a=blocker.corners[i];const auto& b=blocker.corners[(i+1)%4];
        const double dx=double(b[0])-a[0],dy=double(b[1])-a[1];
        const double t=(std::clamp)(((candidate.center[0]-a[0])*dx+(candidate.center[1]-a[1])*dy)/(dx*dx+dy*dy),0.0,1.0);
        if(std::hypot(double(candidate.center[0])-a[0]-t*dx,double(candidate.center[1])-a[1]-t*dy)<=candidate.radius)return false;
    }
    return true;
}
}
GameFsStatus GamePositionRegions::Build(const GameModelClone& model) {
    if(!model.Asset()||!model.Asset()->Live())return {GameFsCode::Unavailable,0,"Region build needs live scene model"};
    std::vector<GameRegionInput> nodes;
    const auto& data=model.Asset()->Data();
    if(data.nodes.size()!=model.Pose().size())return {GameFsCode::Unavailable,0,"Region model/pose extent"};
    for(size_t i=0;i<data.nodes.size();++i)if(data.nodes[i].type==12) {
        const auto& source=model.Pose()[i].emitter;
        nodes.push_back({uint32_t(i),source.first,source.second,source.scalar0,source.scalar1,model.Pose()[i].world});
    }
    return Initialize(nodes);
}
GameFsStatus GamePositionRegions::Initialize(const std::vector<GameRegionInput>& nodes) {
    Close();
    try {
        std::array<std::vector<GameRegionRecord>,3> pending;
        for(const auto& node:nodes) {
            if(node.selector>=pending.size())continue; // classifier836F8 skips other selectors
            Need(node.shape==1||node.shape==2,"Unsupported selected region shape");
            Need(std::isfinite(node.first)&&std::isfinite(node.second)&&node.first>0,"Region dimension/radius");
            Need(node.shape!=1||node.second>0,"Region quad second dimension");
            auto& list=pending[node.selector];Need(list.size()<256,"Source region owner capacity256");
            GameRegionRecord record;record.node=node.node;record.shape=node.shape;record.radius=node.first;
            for(auto value:node.world)Need(std::isfinite(value),"Region world matrix finite");
            record.center=ModelTransform(node.world,{{0,0,0,1}});Finite(record.center);
            if(node.shape==1) {
                const float x=node.second/2,y=node.first/2;
                const std::array<GameVector4,4> local{{{{x,y,0,1}},{{x,-y,0,1}},{{-x,-y,0,1}},{{-x,y,0,1}}}};
                for(size_t i=0;i<4;++i){record.corners[i]=ModelTransform(node.world,local[i]);Finite(record.corners[i]);}
                for(size_t i=0;i<4;++i) {
                    const auto& a=record.corners[i];const auto& b=record.corners[(i+1)%4];
                    const float nx=b[1]-a[1],ny=a[0]-b[0];
                    Need(std::hypot(nx,ny)>0,"Degenerate region edge");
                    record.planes[i]={{nx,ny,-nx*a[0]-ny*a[1]}};
                    for(auto value:record.planes[i])Need(std::isfinite(value),"Region affine coefficient overflow");
                    // Preserve source-ordered inward halfplanes. Mirrored or
                    // degenerate transforms refuse instead of inverting truth.
                    const auto& p=record.planes[i];
                    Need(double(p[0])*record.center[0]+double(p[1])*record.center[1]+p[2]>0,"Region winding/extent unsupported");
                }
            }
            list.push_back(record);
        }
        lists_=std::move(pending);live_=true;return {};
    }catch(const std::exception& e){return {GameFsCode::Bounds,0,e.what()};}
}
GameFsStatus GamePositionRegions::ContainsSecond(GameVector4 point,bool& result) const {
    if(!live_)return {GameFsCode::Unavailable,0,"Compiled region owner retired"};
    try {
        Finite(point);point[2]=1; // source27AD4's temporary affine coordinate
        bool inside=lists_[1].empty();
        for(const auto& region:lists_[1])if(Contains(region,point)){inside=true;break;}
        result=inside;return {};
    }catch(const std::exception& e){return {GameFsCode::Bounds,0,e.what()};}
}
GameFsStatus GamePositionRegions::FallbackSecond(GameVector4 point,GameVector4& result) const {
    if(!live_)return {GameFsCode::Unavailable,0,"Compiled region owner retired"};
    try {
        Finite(point);if(lists_[1].empty()){result=point;return {};}
        point[2]=1;const GameRegionRecord* selected=nullptr;double best=std::numeric_limits<double>::infinity();
        for(const auto& candidate:lists_[1]) {
            bool contained=false;for(const auto& blocker:lists_[2])if(FullyContained(candidate,blocker)){contained=true;break;}
            if(contained)continue;
            const auto distance=Distance(point,candidate.center);
            if(!selected||distance<best){selected=&candidate;best=distance;}
        }
        // Source27648 returns a temporary address; native returns an owned
        // value. It chooses a center, never a nearest edge or actor teleport.
        result=selected?selected->center:point;return {};
    }catch(const std::exception& e){return {GameFsCode::Bounds,0,e.what()};}
}
} // namespace dokodemo::recovered
