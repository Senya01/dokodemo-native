#include "game_graphics_native.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace dokodemo::recovered {
bool GameGraphics::Initialize(const GameGraphicsConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.live || !config.width || !config.height || config.width > 2048
        || config.height > 2048 || config.stride < config.width || config.stride > 4096
        || config.source_pixel_format != 3 || config.scissor_left > config.scissor_right
        || config.scissor_top > config.scissor_bottom || config.scissor_right > config.width
        || config.scissor_bottom > config.height || state_.generation == UINT64_MAX
        || config.initial_matrix_mode >= config.matrix_capacities.size()
        || !config.matrix_capacities[config.initial_matrix_mode]) return false;
    uint32_t matrix_count = 0;
    std::array<uint32_t, 4> offsets{};
    for (size_t i = 0; i < offsets.size(); ++i) {
        if (config.matrix_capacities[i] > 64) return false;
        offsets[i] = matrix_count; matrix_count += config.matrix_capacities[i];
    }
    const auto count = size_t(config.stride) * config.height;
    try {
        std::array<std::vector<uint32_t>, 2> color;
        color[0].assign(count, 0); color[1].assign(count, 0);
        std::vector<uint16_t> depth(count, 0);
        GameMatrix identity{};
        identity[0] = identity[5] = identity[10] = identity[15] = 1;
        std::vector<GameMatrix> matrices(matrix_count, identity);
        color_.swap(color); depth_.swap(depth);
        matrices_.swap(matrices); matrix_offsets_ = offsets;
    } catch (const std::bad_alloc&) { return false; }
    const auto generation = state_.generation + 1;
    state_ = {};
    state_.config = config; state_.generation = generation;
    state_.initialized = state_.live = true;
    state_.color_bytes = uint64_t(count) * sizeof(uint32_t) * 2;
    state_.depth_bytes = uint64_t(count) * sizeof(uint16_t);
    state_.matrix_mode = config.initial_matrix_mode;
    state_.matrix_bytes = uint64_t(matrix_count) * sizeof(GameMatrix);
    for (size_t i = 0; i < config.matrix_capacities.size(); ++i)
        state_.matrix_depths[i] = config.matrix_capacities[i] ? 1u : 0u;
    front_ = 1; back_ = 0;
    return true;
}
bool GameGraphics::Inside(int32_t x, int32_t y) const noexcept {
    const auto& c = state_.config;
    return x >= 0 && y >= 0 && uint32_t(x) < c.width && uint32_t(y) < c.height
        && (!c.scissor || (uint32_t(x) >= c.scissor_left && uint32_t(x) < c.scissor_right
            && uint32_t(y) >= c.scissor_top && uint32_t(y) < c.scissor_bottom));
}
bool GameGraphics::SetScissor(int32_t left,int32_t top,int32_t right,int32_t bottom){
    std::lock_guard<std::mutex> lock(mutex_);if(!state_.live||left>right||top>bottom)return false;
    auto& c=state_.config;
    c.scissor_left=uint32_t((std::clamp)(left,0,int32_t(c.width)));
    c.scissor_right=uint32_t((std::clamp)(right,0,int32_t(c.width)));
    c.scissor_top=uint32_t((std::clamp)(top,0,int32_t(c.height)));
    c.scissor_bottom=uint32_t((std::clamp)(bottom,0,int32_t(c.height)));return true;
}
void GameGraphics::Pixel(uint32_t x, uint32_t y, uint32_t color, uint16_t depth) noexcept {
    const auto& c = state_.config;
    const auto at = size_t(y) * c.stride + x;
    const uint32_t alpha = color >> 24;
    if ((c.alpha_test && alpha == 0) || (c.depth_test && depth < depth_[at])) return;
    if (c.blend) {
        const uint32_t destination = color_[back_][at];
        uint32_t mixed = 0xFF000000;
        for (unsigned shift = 0; shift < 24; shift += 8) {
            const uint32_t source = (color >> shift) & 255;
            const uint32_t target = (destination >> shift) & 255;
            mixed |= ((source * alpha + target * (255 - alpha) + 127) / 255) << shift;
        }
        color = mixed;
    }
    color_[back_][at] = color;
    if (c.depth_write) depth_[at] = depth;
}
bool GameGraphics::Clear(uint32_t color, uint16_t depth) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live) return false;
    // Clear ignores viewport/scissor/alpha/depth tests, like a buffer clear.
    std::fill(color_[back_].begin(), color_[back_].end(), color);
    std::fill(depth_.begin(), depth_.end(), depth);
    return true;
}
bool GameGraphics::FillRectangle(int32_t left, int32_t top, int32_t right, int32_t bottom,
                                  uint32_t color, uint16_t depth) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live || right < left || bottom < top) return false;
    left = (std::max)(left, 0); top = (std::max)(top, 0);
    right = (std::min)(right, int32_t(state_.config.width));
    bottom = (std::min)(bottom, int32_t(state_.config.height));
    for (int32_t y = top; y < bottom; ++y)
        for (int32_t x = left; x < right; ++x) if (Inside(x, y)) Pixel(uint32_t(x), uint32_t(y), color, depth);
    ++state_.draw_calls;
    return true;
}
bool GameGraphics::DrawTriangle(const std::array<GameVertex, 3>& v) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live) return false;
    for (const auto& vertex : v)
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y)
            || std::abs(vertex.x) > 1048576 || std::abs(vertex.y) > 1048576) return false;
    const auto edge = [](const GameVertex& a, const GameVertex& b, float x, float y) {
        return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
    };
    const float area = edge(v[0], v[1], v[2].x, v[2].y);
    if (std::abs(area) < 0.00001f) return false;
    // Native screen coordinates have y increasing downwards.
    const bool counter_clockwise = area < 0;
    if (state_.config.cull_face && counter_clockwise != state_.config.front_face_counter_clockwise) {
        ++state_.draw_calls;
        return true;
    }
    const int left = (std::max)(0, int(std::floor((std::min)({v[0].x, v[1].x, v[2].x}))));
    const int top = (std::max)(0, int(std::floor((std::min)({v[0].y, v[1].y, v[2].y}))));
    const int right = (std::min)(int(state_.config.width), int(std::ceil((std::max)({v[0].x, v[1].x, v[2].x}))));
    const int bottom = (std::min)(int(state_.config.height), int(std::ceil((std::max)({v[0].y, v[1].y, v[2].y}))));
    for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
        if (!Inside(x, y)) continue;
        const float w0 = edge(v[1], v[2], float(x) + 0.5f, float(y) + 0.5f) / area;
        const float w1 = edge(v[2], v[0], float(x) + 0.5f, float(y) + 0.5f) / area;
        const float w2 = 1 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        uint32_t color = state_.config.smooth_shading ? 0 : v[0].color;
        for (unsigned shift = 0; state_.config.smooth_shading && shift < 32; shift += 8) {
            const auto value = uint32_t(std::lround(float((v[0].color >> shift) & 255) * w0
                + float((v[1].color >> shift) & 255) * w1 + float((v[2].color >> shift) & 255) * w2));
            color |= (std::min)(value, uint32_t(255)) << shift;
        }
        const auto depth = uint16_t(std::lround(float(v[0].depth) * w0 + float(v[1].depth) * w1 + float(v[2].depth) * w2));
        Pixel(uint32_t(x), uint32_t(y), color, depth);
    }
    ++state_.draw_calls;
    return true;
}
bool GameGraphics::MeshPixel(uint32_t x,uint32_t y,uint32_t color,uint16_t depth,
                             const GameRasterPolicy& policy) noexcept {
    const auto at=size_t(y)*state_.config.stride+x;
    const auto alpha=color>>24;
    if(!alpha)return false;
    if(policy.match_existing_depth){
        if(!depth_[at]||std::abs(int(depth)-int(depth_[at]))>4)return false;
    }else if(policy.depth_test&&depth<depth_[at])return false;
    if(policy.blend && alpha<255) {
        const auto dest=color_[back_][at];uint32_t mixed=0xFF000000;
        for(unsigned shift=0;shift<24;shift+=8)
            mixed|=((((color>>shift)&255)*alpha+((dest>>shift)&255)*(255-alpha)+127)/255)<<shift;
        color=mixed;
    }
    color_[back_][at]=color;
    if(policy.depth_write)depth_[at]=depth;
    return true;
}
bool GameGraphics::DrawMeshTriangle(const std::array<GameMeshVertex,3>& vertices,GameTextureSpan texture,
                                    GameRasterPolicy policy) {
    if(policy.match_existing_depth&&(!policy.depth_test||policy.depth_write))return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if(!state_.live || (texture.pixels && (!texture.width||!texture.height||texture.width>4096||texture.height>4096
        ||uint64_t(texture.width)*texture.height!=texture.count))
        || (!texture.pixels && (texture.width||texture.height||texture.count)))return false;
    struct Vertex {std::array<float,4> p,color;float u,v;};
    std::vector<Vertex> polygon;polygon.reserve(12);
    for(const auto& v:vertices) {
        for(auto x:v.clip)if(!std::isfinite(x)||std::abs(x)>1e12f)return false;
        if(!std::isfinite(v.u)||!std::isfinite(v.v)||std::abs(v.u)>1e8f||std::abs(v.v)>1e8f)return false;
        Vertex p{v.clip,{},v.u,v.v};
        for(unsigned j=0;j<4;++j)p.color[j]=float((v.color>>(8*j))&255);
        polygon.push_back(p);
    }
    ++state_.mesh_triangles;
    //Clip against the native OpenGL-style homogeneous frustum plus positiveW.
    for(unsigned plane=0;plane<7 && !polygon.empty();++plane) {
        const auto distance=[plane](const Vertex& v){
            if(plane==6)return v.p[3]-0.00001f;
            return v.p[3]+((plane&1)?-v.p[plane/2]:v.p[plane/2]);
        };
        std::vector<Vertex> next;next.reserve(12);
        auto a=polygon.back();float da=distance(a);
        for(const auto& b:polygon) {
            const auto db=distance(b);
            if((da>=0)!=(db>=0)) {
                const auto t=da/(da-db);Vertex cut{};
                for(unsigned j=0;j<4;++j){cut.p[j]=a.p[j]+(b.p[j]-a.p[j])*t;cut.color[j]=a.color[j]+(b.color[j]-a.color[j])*t;}
                cut.u=a.u+(b.u-a.u)*t;cut.v=a.v+(b.v-a.v)*t;next.push_back(cut);
            }
            if(db>=0)next.push_back(b);
            a=b;da=db;
        }
        polygon.swap(next);
    }
    if(polygon.size()<3)return true;
    struct Screen {float x,y,z,invw,u,v;std::array<float,4> color;};
    const auto screen=[this](const Vertex& v){
        const auto inv=1/v.p[3];Screen s{};
        s.x=(v.p[0]*inv+1)*float(state_.config.width)*.5f;
        s.y=(1-v.p[1]*inv)*float(state_.config.height)*.5f;
        s.z=v.p[2]*inv;s.invw=inv;s.u=v.u*inv;s.v=v.v*inv;
        for(unsigned j=0;j<4;++j)s.color[j]=v.color[j]*inv;
        return s;
    };
    const auto edge=[](const Screen& a,const Screen& b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};
    for(size_t fan=2;fan<polygon.size();++fan) {
        std::array<Screen,3> p{{screen(polygon[0]),screen(polygon[fan-1]),screen(polygon[fan])}};
        const auto area=edge(p[0],p[1],p[2].x,p[2].y);
        if(std::abs(area)<.00001f)continue;
        if(!policy.two_sided && state_.config.cull_face && ((area<0)!=state_.config.front_face_counter_clockwise))continue;
        ++state_.clipped_triangles;if(texture.pixels)++state_.textured_triangles;
        const auto left=(std::max)(0,int(std::floor((std::min)({p[0].x,p[1].x,p[2].x}))));
        const auto top=(std::max)(0,int(std::floor((std::min)({p[0].y,p[1].y,p[2].y}))));
        const auto right=(std::min)(int(state_.config.width),int(std::ceil((std::max)({p[0].x,p[1].x,p[2].x}))));
        const auto bottom=(std::min)(int(state_.config.height),int(std::ceil((std::max)({p[0].y,p[1].y,p[2].y}))));
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
            if(!Inside(x,y))continue;
            const auto a=edge(p[1],p[2],float(x)+.5f,float(y)+.5f)/area;
            const auto b=edge(p[2],p[0],float(x)+.5f,float(y)+.5f)/area;
            const auto c=1-a-b;
            if(a<0||b<0||c<0)continue;
            const auto denominator=a*p[0].invw+b*p[1].invw+c*p[2].invw;
            if(denominator<=0)continue;
            uint32_t texel=0xFFFFFFFF;
            if(texture.pixels) {
                auto u=(a*p[0].u+b*p[1].u+c*p[2].u)/denominator;
                auto v=(a*p[0].v+b*p[1].v+c*p[2].v)/denominator;
                if(policy.repeat){u-=std::floor(u);v-=std::floor(v);}
                else {u=(std::clamp)(u,0.0f,1.0f);v=(std::clamp)(v,0.0f,1.0f);}
                const auto tx=(std::min)(texture.width-1,uint32_t(u*float(texture.width)));
                const auto ty=(std::min)(texture.height-1,uint32_t(v*float(texture.height)));
                texel=texture.pixels[size_t(ty)*texture.width+tx];
            }
            uint32_t color=0;
            for(unsigned j=0;j<4;++j) {
                const auto channel=(a*p[0].color[j]+b*p[1].color[j]+c*p[2].color[j])/denominator;
                const auto value=uint32_t(std::lround((std::clamp)(channel,0.0f,255.0f)));
                const auto scale=policy.windows_dvs_textured_modulation2x&&texture.pixels&&j<3?2u:1u;
                // Scale the product, not the vertex channel: preserve dark
                // texels and saturate only after modulation. Alpha stays1x.
                color|=(std::min)(255u,(scale*value*((texel>>(8*j))&255)+127)/255)<<(8*j);
            }
            const auto z=(std::clamp)(a*p[0].z+b*p[1].z+c*p[2].z,-1.0f,1.0f);
            const auto depth=uint16_t(std::lround((1-z)*.5f*65535));
            if(MeshPixel(uint32_t(x),uint32_t(y),color,depth,policy))++state_.mesh_pixels;
        }
        ++state_.draw_calls;
    }
    return true;
}
bool GameGraphics::DrawContactEllipse(const GameMatrix &m,float cx,float cy,float z,
                                      float rx,float ry,float opacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!state_.live||!std::isfinite(cx)||!std::isfinite(cy)||!std::isfinite(z)||
       !std::isfinite(rx)||!std::isfinite(ry)||!std::isfinite(opacity)||rx<=0||ry<=0||
       rx>1e6f||ry>1e6f||opacity<0||opacity>1)return false;
    for(float v:m)if(!std::isfinite(v)||std::abs(v)>1e12f)return false;
    if(opacity==0)return true;
    double min_x=1e30,max_x=-1e30,min_y=1e30,max_y=-1e30;
    for(int sx:{-1,1})for(int sy:{-1,1}){
        const double x=double(cx)+sx*rx,y=double(cy)+sy*ry;
        const double w=m[3]*x+m[7]*y+double(m[11])*z+m[15];
        if(w<=1e-5)return true; // conservative omission at/behind the eye plane
        const double nx=(m[0]*x+m[4]*y+double(m[8])*z+m[12])/w;
        const double ny=(m[1]*x+m[5]*y+double(m[9])*z+m[13])/w;
        min_x=(std::min)(min_x,nx);max_x=(std::max)(max_x,nx);
        min_y=(std::min)(min_y,ny);max_y=(std::max)(max_y,ny);
    }
    if(max_x<-1||min_x>1||max_y<-1||min_y>1)return true;
    const auto left=int(std::floor(((std::clamp)(min_x,-1.0,1.0)+1)*.5*state_.config.width));
    const auto right=(std::min)(int(state_.config.width),int(std::ceil(((std::clamp)(max_x,-1.0,1.0)+1)*.5*state_.config.width)));
    const auto top=int(std::floor((1-(std::clamp)(max_y,-1.0,1.0))*.5*state_.config.height));
    const auto bottom=(std::min)(int(state_.config.height),int(std::ceil((1-(std::clamp)(min_y,-1.0,1.0))*.5*state_.config.height)));
    GameRasterPolicy policy;policy.depth_write=false;policy.match_existing_depth=true;
    // Invert the current horizontal-plane homography at pixel centres. Each
    // pixel is blended once: no triangulation/shared-edge alpha seams.
    for(int py=top;py<bottom;++py)for(int px=left;px<right;++px){
        if(!Inside(px,py))continue;
        const double nx=2*(double(px)+.5)/state_.config.width-1;
        const double ny=1-2*(double(py)+.5)/state_.config.height;
        const double a=m[0]-nx*m[3],b=m[4]-nx*m[7],c=nx*(double(m[11])*z+m[15])-(double(m[8])*z+m[12]);
        const double d=m[1]-ny*m[3],e=m[5]-ny*m[7],f=ny*(double(m[11])*z+m[15])-(double(m[9])*z+m[13]);
        const auto determinant=a*e-b*d,scale=(std::max)({std::abs(a),std::abs(b),std::abs(d),std::abs(e),1e-30});
        if(std::abs(determinant)<=scale*scale*1e-12)continue;
        const double x=(c*e-b*f)/determinant,y=(a*f-c*d)/determinant;
        const double ux=(x-cx)/rx,uy=(y-cy)/ry,r2=ux*ux+uy*uy;
        if(!std::isfinite(r2)||r2>=1)continue;
        const double w=m[3]*x+m[7]*y+double(m[11])*z+m[15];
        const double clip_z=m[2]*x+m[6]*y+double(m[10])*z+m[14];
        if(w<=1e-5||clip_z<-w||clip_z>w)continue;
        const auto depth=uint16_t(std::lround((1-clip_z/w)*.5*65535));
        const auto alpha=uint32_t(std::lround(opacity*(1-r2)*(1-r2)*255));
        if(MeshPixel(uint32_t(px),uint32_t(py),alpha<<24,depth,policy))++state_.mesh_pixels;
    }
    ++state_.draw_calls;
    return true;
}
bool GameGraphics::DrawRadialFade(float x, float y, float inner, float outer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(inner)
        || !std::isfinite(outer) || inner < 0 || outer < inner) return false;
    for (uint32_t row = 0; row < state_.config.height; ++row) {
        for (uint32_t col = 0; col < state_.config.width; ++col) {
            if (!Inside(int32_t(col), int32_t(row))) continue;
            const float dx = float(col) + 0.5f - x, dy = float(row) + 0.5f - y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= inner && outer > inner) continue;
            const float coverage = outer == inner ? 1.0f : (std::clamp)((distance - inner) / (outer - inner), 0.0f, 1.0f);
            Pixel(col, row, uint32_t(std::lround(coverage * 255)) << 24, 0);
        }
    }
    ++state_.draw_calls; return true;
}
bool GameGraphics::DrawImage(const std::vector<uint32_t>& pixels, uint32_t width, uint32_t height,
                               int32_t left, int32_t top, uint32_t modulation,uint16_t depth) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live || !width || !height || width > 4096 || height > 4096
        || pixels.size() != size_t(width) * height || left < -1048576 || left > 1048576
        || top < -1048576 || top > 1048576) return false;
    for (uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
        const auto dx=left+int32_t(x),dy=top+int32_t(y);
        if(!Inside(dx,dy))continue;
        uint32_t color=0;
        const auto original=pixels[size_t(y)*width+x];
        for(unsigned shift=0;shift<32;shift+=8)
            color|=((((original>>shift)&255)*((modulation>>shift)&255)+127)/255)<<shift;
          Pixel(uint32_t(dx),uint32_t(dy),color,depth);
    }
    ++state_.draw_calls;return true;
}
bool GameGraphics::ProjectClipVertex(const GameClipVertex& input, GameVertex& output) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live || !std::isfinite(input.x) || !std::isfinite(input.y) || !std::isfinite(input.z)
        || input.x < -1 || input.x > 1 || input.y < -1 || input.y > 1 || input.z < -1 || input.z > 1) return false;
    const auto& c = state_.config;
    GameVertex vertex;
    vertex.x = float(int64_t(c.viewport_x) - c.offset_x) + input.x * float(c.width) * 0.5f;
    vertex.y = float(int64_t(c.viewport_y) - c.offset_y) - input.y * float(c.height) * 0.5f;
    vertex.depth = uint16_t(std::lround((float(c.depth_near) + float(c.depth_far)) * 0.5f
        + input.z * (float(c.depth_far) - float(c.depth_near)) * 0.5f));
    vertex.color = input.color;
    output = vertex;
    return true;
}
bool GameGraphics::SetMatrixMode(uint32_t mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live || mode >= state_.config.matrix_capacities.size() || !state_.matrix_depths[mode]) return false;
    state_.matrix_mode = mode; return true;
}
bool GameGraphics::LoadMatrix(const GameMatrix& matrix) {
    for (const auto value : matrix) if (!std::isfinite(value)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live) return false;
    const auto mode = state_.matrix_mode;
    matrices_[matrix_offsets_[mode] + state_.matrix_depths[mode] - 1] = matrix;
    return true;
}
bool GameGraphics::PushMatrix() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live) return false;
    const auto mode = state_.matrix_mode, count = state_.matrix_depths[mode];
    if (count >= state_.config.matrix_capacities[mode]) return false;
    matrices_[matrix_offsets_[mode] + count] = matrices_[matrix_offsets_[mode] + count - 1];
    ++state_.matrix_depths[mode]; return true;
}
bool GameGraphics::PopMatrix() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live || state_.matrix_depths[state_.matrix_mode] <= 1) return false;
    --state_.matrix_depths[state_.matrix_mode]; return true;
}
bool GameGraphics::ReadMatrix(uint32_t mode, GameMatrix& output) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live || mode >= state_.matrix_depths.size() || !state_.matrix_depths[mode]) return false;
    output = matrices_[matrix_offsets_[mode] + state_.matrix_depths[mode] - 1]; return true;
}
bool GameGraphics::SwapBuffers() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live || state_.frame_sequence == UINT64_MAX) return false;
    front_ = back_; back_ ^= 1;
    state_.draw_buffer_index = back_; state_.display_buffer_index = front_;
    ++state_.frame_sequence;
    return true;
}
bool GameGraphics::CopyFront(GameGraphicsFrame& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live || !state_.frame_sequence) return false;
    GameGraphicsFrame frame;
    frame.width = state_.config.width; frame.height = state_.config.height; frame.stride = state_.config.stride;
    frame.generation = state_.generation; frame.sequence = state_.frame_sequence;
    frame.diagnostic = state_.diagnostic_frame; frame.pixels = color_[front_];
    out = std::move(frame);
    return true;
}
bool GameGraphics::RecordPresented(uint64_t generation, uint64_t sequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.live || generation != state_.generation || !sequence || sequence > state_.frame_sequence) return false;
    ++state_.present_count; state_.last_presented_sequence = sequence;
    return true;
}
bool GameGraphics::DrawDiagnosticFrame() {
    // Deliberately uses the real drawing API. This is a test image, not a
    // substitute title screen or a claim that a game draw producer ran.
    if (!Clear(0xFF162533, 0)) return false;
    if (!FillRectangle(16, 16, 144, 80, 0xFFFF3030, 100)
        || !FillRectangle(176, 16, 304, 80, 0xFF30FF30, 100)
        || !FillRectangle(336, 16, 464, 80, 0xFF3030FF, 100)
        || !FillRectangle(24, 176, 456, 248, 0xFF52677A, 100)
        || !FillRectangle(64, 192, 240, 232, 0x80FFFFFF, 200)
        || !DrawTriangle({{{144, 96, 100, 0xFFFF4040}, {240, 164, 100, 0xFF4040FF}, {336, 96, 100, 0xFF40FF40}}})) return false;
    { std::lock_guard<std::mutex> lock(mutex_); state_.diagnostic_frame = true; }
    return SwapBuffers();
}
GameGraphicsSnapshot GameGraphics::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_); return state_;
}
void GameGraphics::Close() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& color : color_) std::vector<uint32_t>().swap(color);
    std::vector<uint16_t>().swap(depth_);
    std::vector<GameMatrix>().swap(matrices_);
    state_.live = false; state_.color_bytes = state_.depth_bytes = state_.matrix_bytes = 0;
    state_.matrix_depths = {};
}
} // namespace dokodemo::recovered
