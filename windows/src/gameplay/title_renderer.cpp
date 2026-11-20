#include "title_renderer.h"
#include "core_hash.h"
#include "display.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
using Bytes = std::vector<std::uint8_t>;
using Values = std::array<float, 4>;

Bytes Read(const std::filesystem::path& path, size_t length, const char* sha) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() != static_cast<std::streamoff>(length))
        throw std::runtime_error("Incorrect title media size: " + path.filename().string());
    Bytes bytes(length);
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length)) ||
        core::sha256_hex(bytes.data(), length) != sha)
        throw std::runtime_error("Incorrect title media: " + path.filename().string());
    return bytes;
}

unsigned U16(const Bytes& b, size_t at) { return b.at(at) | unsigned(b.at(at+1)) << 8; }
int S16(const Bytes& b, size_t at) { return static_cast<std::int16_t>(U16(b, at)); }
std::uint32_t U32(const Bytes& b, size_t at) { return U16(b, at) | U16(b, at+2) << 16; }
float F32(const Bytes& b, size_t at) {
    const auto bits = U32(b, at);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

struct Track {
    struct Key { Values value{}; unsigned mode; int frame; };
    unsigned node, kind;
    std::vector<Key> keys;

    Values Sample(int frame, bool relative=false) const {
        const auto& base = keys.front().value;
        // First key records the initial transform. The sprite track uses absolute
        // texture IDs, including complemented root references. Pose keys after
        // the initial key apply relative to the base (BOOT 96C48 / 96730).
        const auto upper = std::upper_bound(keys.begin()+1, keys.end(), frame,
            [](int f, const Key& k){ return f < k.frame; });
        if (upper <= keys.begin()+1) {
            if (!relative || !kind) return base;
            Values neutral{};
            neutral.fill(kind==1 ? 1.f : kind==4 ? 128.f : 0.f);
            return neutral;
        }
        const Key& key = upper[-1];
        Values value = key.value;
        if (!kind) return value;
        if (key.mode == 2 && upper != keys.end()) {
            const float t = float(frame-key.frame) / float(upper->frame-key.frame);
            for (unsigned i=0; i<4; ++i)
                value[i] += (upper->value[i]-value[i])*t;
        }
        if (relative) return value;
        for (unsigned i=0; i<4; ++i) {
            if (kind == 1) value[i] *= base[i];
            else if (kind == 4) value[i] = float((int(base[i])*int(value[i])) >> 7);
            else value[i] += base[i];
        }
        return value;
    }
};
struct Pose {
    int sprite = 0;
    float x=0, y=0, sx=1, sy=1, angle=0;
    Values color{128,128,128,128};
};
struct Matrix { float a=1,b=0,c=0,d=1,x=0,y=0; };
struct Texture { int x,y,width,height; size_t at; };

// GE TEXFILTER 0x107 in the title pass uses linear magnification. Texture
// coordinates designate texel boundaries; samples at half-integers are exact.
std::array<unsigned,4> SampleTexture(const Texture& t, const Bytes& pixels,
                                     float u, float v, bool exact) {
    std::array<unsigned,4> rgba{};
    if (exact) {
        const auto p=t.at+4*(int(v)*t.width+int(u));
        std::copy_n(pixels.data()+p,4,rgba.begin());
        return rgba;
    }
    u=std::clamp(u-0.5f,0.f,float(t.width-1));
    v=std::clamp(v-0.5f,0.f,float(t.height-1));
    const int x=int(u), y=int(v);
    const auto* p=pixels.data()+t.at+4*(y*t.width+x);
    const int right=(x+1<t.width)?4:0, down=(y+1<t.height)?4*t.width:0;
    const float fx=u-x, fy=v-y;
    for(int i=0;i<4;++i) {
        const float a=p[i]+fx*(p[right+i]-p[i]);
        const float b=p[down+i]+fx*(p[down+right+i]-p[down+i]);
        rgba[i]=unsigned(a+fy*(b-a)+0.5f);
    }
    return rgba;
}

void Blit(const Texture& t, const Bytes& src, const Matrix& m, const Values& color,
          Bytes& dst, size_t offset) {
    // GuiTitle submits signed 16-bit screen vertices, after the screen-center
    // translation. Fractional object transforms therefore snap at the corners,
    // before the rasterizer interpolates the texel coordinates.
    const auto X = [&](int x,int y){return float(int(240.f+m.x+m.a*x+m.c*y));};
    const auto Y = [&](int x,int y){return float(int(136.f+m.y+m.b*x+m.d*y));};
    const float x0=X(t.x,t.y), y0=Y(t.x,t.y);
    const float dx=X(t.x+t.width,t.y)-x0, dy=Y(t.x+t.width,t.y)-y0;
    const float ex=X(t.x,t.y+t.height)-x0, ey=Y(t.x,t.y+t.height)-y0;
    const float det=dx*ey-ex*dy;
    if (!det) return;
    const bool exact=(dy==0 && ex==0 && std::abs(dx)==t.width && std::abs(ey)==t.height) ||
                     (dx==0 && ey==0 && std::abs(dy)==t.width && std::abs(ex)==t.height);
    const int left=std::max(0,int(std::floor(x0+std::min(0.f,dx)+std::min(0.f,ex))));
    const int top=std::max(0,int(std::floor(y0+std::min(0.f,dy)+std::min(0.f,ey))));
    const int right=std::min(480,int(std::ceil(x0+std::max(0.f,dx)+std::max(0.f,ex))));
    const int bottom=std::min(272,int(std::ceil(y0+std::max(0.f,dy)+std::max(0.f,ey))));
    std::array<unsigned,4> tint;
    for (int i=0;i<4;++i) tint[i]=(unsigned(color[i])*255/128)&255;
    for (int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
        const float u=t.width*(ey*(x+0.5f-x0)-ex*(y+0.5f-y0))/det;
        const float v=t.height*(dx*(y+0.5f-y0)-dy*(x+0.5f-x0))/det;
        if (!(u>=0 && u<t.width && v>=0 && v<t.height)) continue;
        const auto pixel=SampleTexture(t,src,u,v,exact);
        auto out=&dst[offset+4*(y*512+x)];
        const unsigned alpha=pixel[3]*tint[3]/255;
        for (unsigned i=0;i<3;++i) {
            const unsigned mod=(pixel[i]*tint[i]+127)/255;
            out[i]=static_cast<uint8_t>((mod*alpha+out[i]*(255-alpha)+127)/255);
        }
    }
}
}

struct Cb2aSceneData {
    struct Node { unsigned sibling, child, flags; };
    Bytes pixels;
    std::vector<Texture> textures;
    std::vector<Node> nodes;
    std::vector<std::vector<Track>> clips;

    void Apply(std::vector<Pose>& poses,unsigned group,int frame,bool relative) const {
        for (const auto& track:clips.at(group)) {
            const auto value=track.Sample(frame,relative);
            Pose& p=poses.at(track.node);
            switch(track.kind) {
                case 0: p.sprite=int(value[0]); break;
                case 1: p.sx=(relative?p.sx:1)*value[0]; p.sy=(relative?p.sy:1)*value[1]; break;
                case 2: p.angle=(relative?p.angle:0)+value[0]; break;
                case 3: p.x=(relative?p.x:0)+value[0]; p.y=(relative?p.y:0)+value[1]; break;
                case 4:
                    if (!relative) p.color=value;
                    else for(unsigned i=0;i<4;++i)
                        p.color[i]=float(std::min(255,(int(p.color[i])*int(value[i]))>>7));
                    break;
            }
        }
    }

    void Bind(std::vector<Pose>& poses, unsigned root, unsigned clip) const {
        // 96C48 resets this subtree's transforms before sampling the base.
        // The sprite/reference survives if the new clip has no sprite key.
        auto reset = [&](auto&& self, unsigned index)->void {
            const int sprite = poses.at(index).sprite;
            poses[index] = Pose{};
            poses[index].sprite = sprite;
            for (unsigned n=nodes.at(index).child; n; n=nodes.at(n).sibling) self(self,n);
        };
        reset(reset,root);
        Apply(poses,clip,0,false);
    }

    void Draw(unsigned group, unsigned frame, unsigned variant, const Matrix& parent,
              Values parent_color, bool foreground, Bytes& vram, size_t offset,
              const std::vector<Pose>* editor=nullptr,
              const std::function<void(unsigned,int,int)>* callback=nullptr,
              const std::array<unsigned,8>* profile_frames=nullptr,
              std::array<unsigned,2> arrow_frames={1,1}) const {
        std::vector<Pose> sampled;
        if (!editor) {
            sampled.resize(nodes.size());
            Apply(sampled,group,frame,false);
        }
        const auto& poses=editor?*editor:sampled;
        auto visit=[&](auto&& self, unsigned index, Matrix matrix, Values color)->void {
            const auto& node=nodes.at(index);
            const auto& p=poses.at(index);
            if (!(node.flags&2)) {matrix.a=matrix.d=1;matrix.b=matrix.c=0;}
            if (!(node.flags&4)) matrix.x=matrix.y=0;
            // CB2A encodes quarter turns as the nearest float to pi/2. The GE
            // vertices of the corresponding horizontal/vertical strips land on
            // integer coordinates. A libm cosine residue can drop an entire row
            // when the final signed screen coordinate is truncated.
            constexpr float quarter_turn=1.57079632679489661923f;
            const float cs=std::abs(p.angle)==quarter_turn ? 0.f : std::cos(-p.angle);
            const float sn=std::sin(-p.angle);
            Matrix m{(matrix.a*cs+matrix.c*sn)*p.sx,(matrix.b*cs+matrix.d*sn)*p.sx,
                     (matrix.c*cs-matrix.a*sn)*p.sy,(matrix.d*cs-matrix.b*sn)*p.sy,
                     matrix.x+matrix.a*p.x+matrix.c*p.y,matrix.y+matrix.b*p.x+matrix.d*p.y};
            for (unsigned i=0;i<4;++i)
                color[i]=float(std::min(255,((node.flags&8 ? int(color[i]):128)*int(p.color[i])) >> 7));
            if (p.sprite<0) {
                const unsigned nested=unsigned(~p.sprite);
                unsigned selected=1;
                if (nested==6) selected=variant;
                if (nested==7 || nested==8) selected=arrow_frames[nested-7];
                if (profile_frames && nested>=9 && nested<=16) selected=(*profile_frames)[nested-9];
                Draw(nested,selected,variant,m,color,foreground,vram,offset,editor,callback,profile_frames,arrow_frames);
            } else if (p.sprite>0 && (editor || foreground==(group>=5 && group!=18))) {
                Blit(textures.at(p.sprite-1),pixels,m,color,vram,offset);
            }
            if (callback && *callback)
                (*callback)(index,int(240+m.x),int(136+m.y));
            for (unsigned child=node.child;child;child=nodes.at(child).sibling)
                self(self,child,m,color);
        };
        visit(visit,group,parent,parent_color);
    }
};

bool TitleRenderer::Load(const std::filesystem::path& directory, std::string& error) {
    return LoadResource(directory,Resource::title,error);
}

bool TitleRenderer::LoadSoftkey(const std::filesystem::path& directory, std::string& error) {
    return LoadResource(directory,Resource::softkey,error);
}

bool TitleRenderer::LoadSelect(const std::filesystem::path& directory, std::string& error) {
    return LoadResource(directory,Resource::select,error);
}

bool TitleRenderer::LoadTrc(const std::filesystem::path& directory, std::string& error) {
    return LoadResource(directory,Resource::trc,error);
}

static auto ReadScene(const std::filesystem::path& directory,const Cb2aMedia& media) {
    auto scene=std::make_shared<Cb2aSceneData>();
    const auto cb=Read(directory/media.clip,media.clip_size,media.clip_hash);
    scene->pixels=Read(directory/media.bank,media.bank_size,media.bank_hash);
    size_t at=8;
    for(unsigned i=0;i<U32(scene->pixels,4);++i) {
        const auto tex=U32(cb,8)+i*8;
        const int w=U16(scene->pixels,at), h=U16(scene->pixels,at+2);
        scene->textures.push_back({S16(cb,tex),S16(cb,tex+2),w,h,at+4});
        at+=4+4*w*h;
    }
    for(unsigned i=0;i<0u-U32(cb,12);++i) {
        const auto n=U32(cb,16)+i*8;
        scene->nodes.push_back({U16(cb,n),U16(cb,n+2),U32(cb,n+4)});
    }
    scene->clips.resize(0u-U32(cb,20));
    for(unsigned i=0;i<scene->clips.size();++i) for(unsigned k=0;k<5;++k) {
        const auto channel=U32(cb,24)+i*52+k*8;
        for(unsigned j=0;j<0u-U32(cb,channel);++j) {
            const auto tr=U32(cb,channel+4)+j*8;
            Track track{U16(cb,tr),k,{}};
            const unsigned stride=(k==1||k==3)?16:12;
            for(int key=0;key<-S16(cb,tr+2);++key) {
                const auto pos=U32(cb,tr+4)+key*stride;
                Values v{};
                if(k==0) v[0]=float(static_cast<std::int32_t>(U32(cb,pos)));
                else if(k==4) for(unsigned c=0;c<4;++c)v[c]=float(cb.at(pos+c));
                else {v[0]=F32(cb,pos); if(stride==16)v[1]=F32(cb,pos+4);}
                track.keys.push_back({v,U32(cb,pos+stride-8),static_cast<std::int32_t>(U32(cb,pos+stride-4))});
            }
            scene->clips[i].push_back(std::move(track));
        }
    }
    return scene;
}

bool TitleRenderer::LoadResource(const std::filesystem::path& directory, Resource resource, std::string& error) {
    try {
        constexpr Cb2aMedia assets[]={
            {"TITLE.CB2A",37363,"04635d29d45ff148c55e3acc3df40dcb0d261bda1146772ecec3b5d00ebc6bd4",
             "scene-textures.bin",1634912,"a9bbecc85e222fc0af996f9ccc66cec0adf5dc2a688eda3b47b21eeeebc7c9e8"},
            {"SOFTKEY.CB2A",54229,"4a4096fecd1478a4bd7e77d62670075f592aca586033adafc77c88961a67e36f",
             "softkey-textures.bin",142000,"257404b799b85ba4677eef5cebca45c65087f0c7751f90b3078bf166b5049490"},
            {"SELECT.CB2A",11307,"6473316977b01dcce46022b643a364efae5f3a9e6da2770b67eda4c42e422d06",
             "select-textures.bin",8864,"ffbe0f582015f9761ba0b8da3e7b19b87ab9ff7daab0b74316424314cf09ed72"},
            {"TRC.CB2A",6940,"ee9fedd4cbf141814dbd01b4edd99aa66c88f0eb88d4079212ce300d5fe52c33",
             "trc-textures.bin",17196,"eb91b97daa8d9ad7ed3c23c5c79bc9c9df35a08495df9a46748a85d788797be6"}
        };
        const auto& media=assets[static_cast<unsigned>(resource)];
        auto scene=ReadScene(directory,media);
        switch(resource) {
            case Resource::title: scene_=std::move(scene); break;
            case Resource::softkey: softkey_=std::move(scene); break;
            case Resource::select: select_=std::move(scene); break;
            case Resource::trc: trc_=std::move(scene); break;
        }
        error.clear(); return true;
    } catch (const std::exception& e) {error=e.what();return false;}
}

bool Cb2aScene::Load(const std::filesystem::path& directory,const Cb2aMedia& media,std::string& error) {
    try {
        scene_=ReadScene(directory,media);
        error.clear();
        return true;
    } catch (const std::exception& e) { error=e.what(); return false; }
}

bool Cb2aScene::Draw(unsigned root,const std::vector<Cb2aClip>& clips,Bytes& vram,size_t offset,
                     const Cb2aPlacement& transform,const Cb2aCallback& callback,
                     const std::vector<Cb2aNode>& overrides) const {
    constexpr size_t surface=4*(512*(272-1)+480);
    if (!scene_ || offset>vram.size() || surface>vram.size()-offset || root>=scene_->nodes.size()) return false;
    std::vector<Pose> poses(scene_->nodes.size());
    for (const auto& control:clips) {
        if (control.clip>=scene_->clips.size()) return false;
        scene_->Apply(poses,control.clip,control.frame,control.relative);
    }
    for (const auto& override:overrides) {
        if (override.node>=poses.size()) return false;
        auto& p=poses[override.node];
        if (override.sprite) {
            int id=*override.sprite;
            if ((id>0 && unsigned(id)>scene_->textures.size()) || (id<0 && unsigned(~id)>=scene_->nodes.size()))
                return false;
            p.sprite=id;
        }
        if (override.x) p.x=*override.x;
        if (override.y) p.y=*override.y;
        if (override.sx) p.sx=*override.sx;
        if (override.sy) p.sy=*override.sy;
    }
    const Matrix matrix{transform.sx,0,0,transform.sy,transform.x,transform.y};
    scene_->Draw(root,0,1,matrix,{128,128,128,128},true,vram,offset,&poses,&callback);
    return true;
}

bool TitleRenderer::RenderSoftkey(unsigned clip,unsigned frame,const std::vector<UiClipFrame>& controls,
                                 const std::vector<UiNodeOverride>& overrides,
                                 const std::function<void(unsigned,int,int)>& draw_node,
                                 Bytes& vram,size_t offset) const {
    constexpr size_t surface=4*(512*(272-1)+480);
    if (!softkey_ || offset>vram.size() || surface>vram.size()-offset ||
        clip>=softkey_->clips.size()) return false;
    std::vector<Pose> poses(softkey_->nodes.size());
    // Original GuiSoftkey constructor binds these clips to disjoint subtrees.
    // Bind root 9 again when opening/closing; it owns the animated outer panel.
    constexpr unsigned bindings[][2]={{0,0},{151,1},{1,7},{2,8},{3,9},{4,10},
                                     {7,13},{6,12},{5,11},{8,14},{9,15}};
    for (const auto& binding:bindings) softkey_->Bind(poses,binding[0],binding[1]);
    softkey_->Bind(poses,9,clip);
    for (const auto& item:overrides) {
        if (item.node>=poses.size()) return false;
        if (item.bind_clip) {
            if (*item.bind_clip>=softkey_->clips.size()) return false;
            softkey_->Bind(poses,item.node,*item.bind_clip);
        }
    }
    softkey_->Apply(poses,clip,frame,true);
    for (const auto& control:controls) {
        if (control.clip>=softkey_->clips.size()) return false;
        softkey_->Apply(poses,control.clip,control.frame,true);
    }
    for (const auto& item:overrides) {
        if (item.sprite && (*item.sprite<0 || size_t(*item.sprite)>softkey_->textures.size()))
            return false;
        auto& pose=poses[item.node];
        if (item.sprite) pose.sprite=*item.sprite;
        if (item.x) pose.x=*item.x;
        if (item.y) pose.y=*item.y;
    }
    // GuiSoftkey 1F8B8..1F8D4 widens node 5 of the mode-1 conversion board.
    if (clip==15 || clip==16) poses[5].sx=1.1f;
    softkey_->Draw(9,frame,1,{}, {128,128,128,128},false,vram,offset,&poses,&draw_node);
    return true;
}

bool TitleRenderer::Render(unsigned panel,unsigned frame,unsigned menu_variant,Bytes& vram,
                           size_t offset,bool foreground,std::array<unsigned,2> arrow_frames) const {
    constexpr size_t surface=4*(512*(272-1)+480);
    if(!scene_ || panel>=scene_->clips.size() || offset>vram.size() || surface>vram.size()-offset)
        return false;
    scene_->Draw(panel,frame,menu_variant,{}, {128,128,128,128},foreground,vram,offset,
                 nullptr,nullptr,nullptr,arrow_frames);
    return true;
}

bool TitleRenderer::RenderGuide(unsigned guide,unsigned frame,Bytes& vram,size_t offset) const {
    // 333B0/33468 dispatch the selected navigator as an explicit root at 19+index.
    if (guide>9) return false;
    return Render(19+guide,frame,1,vram,offset,true);
}

bool TitleRenderer::RenderProfile(unsigned active_row,unsigned present_mask,bool foreground,
                                 Bytes& vram,size_t offset,
                                 const std::function<void(unsigned,int,int)>& draw_field) const {
    constexpr size_t surface=4*(512*(272-1)+480);
    if (!scene_ || active_row>7 || offset>vram.size() || surface>vram.size()-offset)
        return false;
    // 32BB4..32CD4 sets the cursor clip and each field's filled/empty/active style.
    std::array<unsigned,8> controls{active_row+1};
    for (unsigned row=0;row<7;++row) {
        const bool selected=row==active_row || (active_row==3 && row==4);
        controls[1+row]=selected ? 3 : (present_mask&(1u<<row)) ? 2 : 1;
    }
    scene_->Draw(4,1,1,{}, {128,128,128,128},foreground,vram,offset,nullptr,nullptr,&controls);
    if (foreground && draw_field) {
        // 32B04 explicitly traverses root 18 with the seven original text callbacks.
        const std::function<void(unsigned,int,int)> on_node=[&](unsigned node,int x,int y) {
            if (node>=375 && node<=381) draw_field(node-375,x,y);
        };
        scene_->Draw(18,1,1,{}, {128,128,128,128},true,vram,offset,nullptr,&on_node);
    }
    return true;
}

bool TitleRenderer::RenderGenderList(unsigned frame,bool closing,unsigned cursor,Bytes& vram,
                                     size_t offset,const std::function<void(unsigned,int,int)>& draw_row) const {
    return RenderSelect(1,frame,closing,cursor,2,0,vram,offset,
        [&](unsigned row,int x,int y,float,int,int){ if (draw_row) draw_row(row,x,y); });
}

bool TitleRenderer::RenderSelect(unsigned mode,unsigned frame,bool closing,unsigned cursor,
                                 unsigned count,float scroll_rows,Bytes& vram,size_t offset,
                                 const DrawSelectItem& draw_item) const {
    constexpr size_t surface=4*(512*(272-1)+480);
    if (!select_ || mode<1 || mode>5 || !count || cursor>=count || count>101 ||
        !std::isfinite(scroll_rows) || scroll_rows<0 || scroll_rows>count ||
        offset>vram.size() || surface>vram.size()-offset) return false;
    // FEF0/FFFC select the window, pose and content width. Title moves node 42
    // through 10864, with the same X/Y offset for each of these five modes.
    struct Layout { unsigned clip, pose; int width, origin; };
    constexpr Layout layouts[]={{3,15,62,8},{11,19,62,12},{5,16,46,8},{7,17,26,8},{9,18,18,12}};
    const auto layout=layouts[mode-1];
    const unsigned clip=layout.clip+unsigned(closing);
    std::vector<Pose> poses(select_->nodes.size());
    select_->Bind(poses,0,0);
    select_->Bind(poses,1,1);
    select_->Bind(poses,1,clip);
    select_->Apply(poses,clip,frame,true);
    select_->Apply(poses,0,layout.pose,true);
    poses[42].x=poses[42].y=float(layout.origin);
    const std::function<void(unsigned,int,int)> on_node=[&](unsigned node,int x,int y) {
        if (node!=34) return;
        const bool calendar=mode==5;
        const int top=calendar ? 0 : y;
        const int bottom=calendar ? 272 : y+18*int(std::min(count,9u));
        for (unsigned item=0;item<count;++item) {
            const int cx=calendar ? x+20*int(item%7) : x;
            const int line=calendar ? y+20*int(item/7) : int(y+18.f*(float(item)-scroll_rows));
            const int height=mode==3 || calendar ? 18 : 17;
            if (line>=bottom || line+18<=top) continue;
            if (item==cursor) {
                // 10B50/C574 uses the raw rectangle color and the source scissor.
                const int left=cx-layout.width/2, right=left+layout.width;
                for (int py=std::max({0,top,line});py<std::min({272,bottom,line+height});++py) {
                    for (int px=std::max(0,left);px<std::min(480,right);++px) {
                        auto dst=vram.data()+offset+4*(512*py+px);
                        dst[0]=234; dst[1]=125; dst[2]=126;
                    }
                }
            }
            if (draw_item)
                draw_item(item,cx,line,calendar && item>=9 ? .75f : 1.f,top,bottom);
        }
    };
    select_->Draw(1,frame,1,{}, {128,128,128,128},false,vram,offset,&poses,&on_node);
    return true;
}

bool TitleRenderer::RenderConfirmation(float progress,unsigned cursor,Bytes& vram,size_t offset,
                                       const std::function<void(int,int,float)>& draw_question,
                                       unsigned columns,unsigned rows) const {
    constexpr size_t surface=4*(512*(272-1)+480);
    if (!trc_ || cursor>1 || !(progress>=0 && progress<=1) ||
        columns<1 || columns>20 || rows<2 || rows>10 ||
        offset>vram.size() || surface>vram.size()-offset) return false;
    // 482E0/48554 lay out the text and the Yes/No row on a 16x23 grid.
    const unsigned frame=columns+20*(rows-1);
    const float width=float(columns*16), height=float(rows*23);
    std::vector<Pose> poses(trc_->nodes.size());
    trc_->Apply(poses,0,frame,false);
    trc_->Apply(poses,1,cursor+1,false);
    auto& panel=poses[0];
    panel.x=128-width*progress*.5f-14;
    panel.y=24-height*progress*.5f-14;
    panel.sx=panel.sy=progress;
    const std::function<void(unsigned,int,int)> on_node=[&](unsigned n,int x,int y) {
        if (n==22 && draw_question) draw_question(x,y,progress);
    };
    trc_->Draw(0,frame,1,{}, {128,128,128,128},false,vram,offset,&poses,&on_node);
    // 48018..4807C draws root 1 explicitly below the text row.
    auto& buttons=poses[1];
    buttons.x=panel.x+width*progress*.5f+14;
    buttons.y=panel.y+height*progress+4;
    buttons.sx=buttons.sy=progress;
    trc_->Draw(1,cursor+1,1,{}, {128,128,128,128},false,vram,offset,&poses);
    return true;
}
}
