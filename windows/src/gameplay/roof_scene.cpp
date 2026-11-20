#include "roof_scene.h"
#include "../recovered/rng_native.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace dokodemo::gameplay {
namespace {
constexpr std::array<float,16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
constexpr std::size_t stride = 512 * 4, height = 272, width = 480;
}

bool RoofScene::Load(const std::filesystem::path& dir, unsigned period, recovered::RngNativeOwner& rng,
                     std::string& error) try {
    // 4848 converts the period of the departing scene to the requested variant.
    constexpr unsigned variants[]{0,1,1,2,0,0};
    if (period >= 6 || !rng.Available())
        throw std::runtime_error("Roof scene requires a current period and game RNG");
    RoofScene next;
    next.resource_ = 66 + variants[period];
    const auto stem = "roof-" + std::to_string(next.resource_);
    if (!next.background_.Load(dir / (stem + ".bin"), error, true) ||
        !next.auxiliary_.Load(dir / (stem + "-aux.bin"), error))
        return false;
    std::ifstream in(dir / (stem + "-controls.bin"), std::ios::binary);
    char magic[4]{};
    in.read(magic,4);
    in.read(reinterpret_cast<char*>(next.placement_.data()),sizeof(float)*4);
    if (!in || std::string(magic,4) != "RUF3" ||
        !std::all_of(next.placement_.begin(),next.placement_.end(),[](float v){return std::isfinite(v);}))
        throw std::runtime_error("Invalid roof scene placement");
    in.read(reinterpret_cast<char*>(&next.child_material_.node), sizeof(std::int32_t));
    in.read(reinterpret_cast<char*>(next.child_material_.source_materials.data()), 4*sizeof(std::int32_t));
    if (!in || next.child_material_.node < 0 ||
        std::any_of(next.child_material_.source_materials.begin(),next.child_material_.source_materials.end(),
                    [](std::int32_t index){ return index < 0; }))
        throw std::runtime_error("Invalid roof child-material map");
    std::vector<Motion> available;
    for (unsigned i=0; i<6; ++i) {
        unsigned char descriptor[3]{};
        in.read(reinterpret_cast<char*>(descriptor),sizeof(descriptor));
        const auto [child,clip,loop] = descriptor;
        if (!in || child > 1 || loop > 1)
            throw std::runtime_error("Invalid roof auxiliary recipe");
        const auto duration = (child ? next.auxiliary_ : next.background_).Duration(clip);
        if (!duration || (!child && clip < 7))
            throw std::runtime_error("Missing roof animation");
        available.push_back({child != 0,loop != 0,clip,0,duration,0});
    }
    const auto base_count = in.get();
    if (base_count < 1 || base_count > 4)
        throw std::runtime_error("Invalid roof main motion count");
    for (int i=0; i<base_count; ++i) {
        const int clip = in.get(), loop = in.get();
        if (clip < 7 || loop < 0 || loop > 1)
            throw std::runtime_error("Invalid roof main motion");
        const auto duration = next.background_.Duration(static_cast<unsigned>(clip));
        if (!duration)
            throw std::runtime_error("Missing roof main animation");
        next.motions_.push_back({false,loop != 0,static_cast<unsigned>(clip),0,duration,0});
    }
    if (in.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Unexpected data after roof motion table");
    for (unsigned i=0; i<7; ++i)
        if (!next.background_.Duration(i))
            throw std::runtime_error("Missing roof seasonal light");

    // 61E4 first chooses a count up to three, then draws without replacement.
    // 635C/662C consumes the additional phase draws after all choices.
    const auto count = rng.NextC3234() % 4;
    for (unsigned i=0; i<count; ++i) {
        const auto index = rng.NextC3234() % available.size();
        next.motions_.push_back(available[index]);
        available.erase(available.begin() + index);
    }
    for (auto& motion : next.motions_)
        if (motion.child)
            motion.child_phase = rng.NextC3234() % 6;
    *this = std::move(next);
    return true;
} catch (const std::exception& e) {
    error = e.what();
    return false;
}

bool RoofScene::Draw(const std::array<float,16>& camera, unsigned season, std::uint32_t light_time,
                     std::vector<std::uint8_t>& vram, std::size_t offset, RoomLighting& lighting,
                     bool* changed_pixels) {
    if (changed_pixels)
        *changed_pixels = false;
    if (!resource_ || season >= 7 || vram.size() < 2 * stride * height + stride * height / 2 ||
        (offset != 0 && offset != stride * height))
        return false;
    main_clips_.clear();
    for (const auto& motion : motions_)
        if (!motion.child)
            main_clips_.emplace_back(motion.clip, motion.time);
    main_clips_.emplace_back(season,light_time);
    lighting = background_.Lighting(main_clips_);
    std::vector<std::uint8_t> before;
    if (changed_pixels)
        before.assign(vram.begin()+offset,vram.begin()+offset+height*stride);
    if (!background_.Draw(main_clips_,identity,camera,vram,offset,&lighting))
        return false;
    if (changed_pixels) {
        for (unsigned y=0; y<height && !*changed_pixels; ++y)
            for (unsigned x=0; x<width && !*changed_pixels; ++x) {
                const auto at = y*stride + x*4;
                *changed_pixels = !std::equal(before.data()+at, before.data()+at+3, vram.data()+offset+at);
            }
    }
    for (auto& motion : motions_) {
        if (motion.child) {
            child_material_.phase = motion.child_phase;
            if (!auxiliary_.Draw({{motion.clip,motion.time}},identity,camera,vram,offset,&lighting,
                                 nullptr,nullptr,&child_material_))
                return false;
        }
        if (motion.time >= motion.duration && motion.loop)
            motion.time = 0;
        motion.time = std::min(motion.duration, motion.time + 160);
    }
    return true;
}
bool OutingScene::Load(const std::filesystem::path& directory,unsigned selector,unsigned period,std::string& error) try {
    constexpr unsigned variants[]{0,1,1,2,0,0}; // BOOT 4848, D3018
    if (!Supports(selector) || period>=std::size(variants))
        throw std::invalid_argument("Unsupported destination selector or period");
    const unsigned resource=30+3*selector+variants[period];
    std::ifstream in(directory/"destination-controls.bin",std::ios::binary);
    std::array<char,4> magic{};
    if (!in.read(magic.data(),magic.size()) || std::string_view(magic.data(),4)!="DSC1")
        throw std::runtime_error("Missing destination registration table");
    OutingScene next;
    for (unsigned i=0;i<54;++i) {
        std::array<std::uint8_t,4> record{};
        std::array<float,4> placement{};
        if (!in.read(reinterpret_cast<char*>(record.data()),record.size()) ||
            !in.read(reinterpret_cast<char*>(placement.data()),sizeof(float)*placement.size()))
            throw std::runtime_error("Truncated destination registration table");
        if (record[0]!=resource) continue;
        const unsigned clip_count=record[1]+record[2];
        if (next.resource_ || (record[1]!=0 && record[1]!=7) || record[2]>4 ||
            !clip_count || (record[3]>>record[2]) ||
            !std::all_of(placement.begin(),placement.end(),[](float n){return std::isfinite(n);}))
            throw std::runtime_error("Invalid destination registration");
        next.resource_=resource;
        next.lights_=record[1];
        std::copy_n(placement.begin(),next.placement_.size(),next.placement_.begin());
        if (!next.background_.Load(directory/("destination-"+std::to_string(resource)+".bin"),error,true))
            return false;
        for (unsigned clip=0;clip<clip_count;++clip) {
            const auto duration=next.background_.Duration(clip);
            if (!duration) throw std::runtime_error("Missing destination animation");
            if (clip>=next.lights_)
                next.motions_.push_back({0,duration,(record[3]&(1u<<(clip-next.lights_)))!=0});
        }
    }
    if (!next.resource_ || in.peek()!=std::char_traits<char>::eof())
        throw std::runtime_error("Unsupported destination registration extent");
    *this=std::move(next);
    return true;
} catch (const std::exception& e) {
    error=e.what();
    return false;
}

bool OutingScene::Draw(const std::array<float,16>& camera,unsigned season,std::uint32_t light_time,
                       std::vector<std::uint8_t>& vram,std::size_t offset,RoomLighting& lighting,
                       bool* changed_pixels) {
    if (!resource_ || season>=7 || vram.size()<2*stride*height+stride*height/2 ||
        (offset!=0 && offset!=stride*height))
        return false;
    std::vector<std::pair<unsigned,std::uint32_t>> clips;
    for (unsigned i=0;i<motions_.size();++i) clips.emplace_back(lights_+i,motions_[i].time);
    if (lights_) clips.emplace_back(season,light_time);
    lighting=background_.Lighting(clips);
    std::vector<std::uint8_t> before;
    if (changed_pixels) before.assign(vram.begin()+offset,vram.begin()+offset+height*stride);
    if (!background_.Draw(clips,identity,camera,vram,offset,&lighting)) return false;
    if (changed_pixels) {
        *changed_pixels=false;
        for (unsigned y=0;y<height && !*changed_pixels;++y)
            for (unsigned x=0;x<width && !*changed_pixels;++x) {
                const auto at=y*stride+x*4;
                *changed_pixels=!std::equal(before.data()+at,before.data()+at+3,vram.data()+offset+at);
            }
    }
    for (auto& clip:motions_) {
        if (clip.loop && clip.time>=clip.duration) clip.time=0;
        clip.time=std::min(clip.duration,clip.time+160);
    }
    return true;
}

} // namespace dokodemo::gameplay
