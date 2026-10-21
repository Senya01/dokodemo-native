#include "model_formats.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace dokodemo::model {
namespace {
struct Reader {
    const Bytes& d; std::size_t p,end;
    explicit Reader(const Bytes& bytes):d(bytes),p(0),end(bytes.size()){}
    Reader(const Bytes& bytes,std::size_t begin,std::size_t stop):d(bytes),p(begin),end(stop) {
        if(begin>stop || stop>bytes.size()) throw FormatError("invalid reader range");
    }
    [[noreturn]] void fail(const char* what) const { std::ostringstream s;s<<what<<" at 0x"<<std::hex<<p<<" (end 0x"<<end<<")";throw FormatError(s.str()); }
    Range take(std::size_t n) { if(n>end-p) fail("truncated record");Range a{p,n};p+=n;return a; }
    Range array(std::size_t n,std::size_t width) { if(width && n>(end-p)/width)fail("array exceeds section");return take(n*width); }
    std::uint8_t u8(){return d[take(1).offset];}
    std::uint16_t u16(){auto a=take(2).offset;return std::uint16_t(d[a]|unsigned(d[a+1])<<8);}
    std::int16_t i16(){auto v=u16();return std::int16_t(v<32768?int(v):int(v)-65536);}
    std::uint32_t u32(){auto a=take(4).offset;return std::uint32_t(d[a])|std::uint32_t(d[a+1])<<8|std::uint32_t(d[a+2])<<16|std::uint32_t(d[a+3])<<24;}
    float f32(){auto v=u32();float f;std::memcpy(&f,&v,4);if(!std::isfinite(f))fail("nonfinite float");return f;}
    void magic(const char* expected){auto a=take(4).offset;if(std::memcmp(d.data()+a,expected,4))fail("wrong magic");}
    void align(){take((4-p%4)%4);}
};
std::vector<float> floats(Reader& r,std::size_t n){if(n>(r.end-r.p)/4)r.fail("float array bounds");std::vector<float> v;v.reserve(n);while(n--)v.push_back(r.f32());return v;}
Vec3 xyz(Reader& r,float factor,std::array<std::int16_t,3>* raw=nullptr){auto x=r.i16(),y=r.i16(),z=r.i16();if(raw)*raw={x,y,z};return {x*factor,y*factor,z*factor};}
Bounds bounds(Reader& r){Bounds b;b.source.offset=r.p;b.boxScale=r.f32();for(auto& v:b.box)v=xyz(r,b.boxScale/32767.0f);for(auto& v:b.sphere)v=r.f32();b.source.bytes=r.p-b.source.offset;return b;}
std::string name(Reader& r,std::size_t n){auto a=r.take(n);std::string s;for(std::size_t i=0;i<n;++i){auto c=r.d[a.offset+i];if(!c)break;if(c>=128)r.fail("non-ASCII resource name");s.push_back(char(c));}return s;}
Topology topology(Reader& r,unsigned op){switch(op){case 0:return Topology::TriangleStrip;case 1:return Topology::TriangleFan;case 2:return Topology::Triangles;case 8:return Topology::LineStrip;case 9:return Topology::Lines;case 16:return Topology::Points;default:r.fail("unsupported primitive opcode");}}
PrimitiveStream primitives(Reader& r,std::size_t nverts){PrimitiveStream s;s.source.offset=r.p;auto words=r.u32();auto span=r.array(words,4);s.source.bytes=r.p-s.source.offset;if(!span.bytes)return s;Reader q(r.d,span.offset,span.offset+span.bytes);auto count=q.i16();if(count<0)q.fail("negative primitive count");for(int j=0;j<count;++j){Primitive a;a.source.offset=q.p;auto desc=q.u16();a.opcode=desc>>8;a.topology=topology(q,a.opcode);auto n=unsigned(desc&255u);q.array(n,2);q.p-=n*2;a.indices.reserve(n);for(unsigned k=0;k<n;++k){auto v=q.i16();if(v<0 || std::size_t(v)>=nverts)q.fail("primitive index outside vertices");a.indices.push_back(std::uint16_t(v));}if(a.topology==Topology::Triangles && n%3)q.fail("incomplete triangle list");a.source.bytes=q.p-a.source.offset;s.primitives.push_back(std::move(a));}if(q.end-q.p>2)q.fail("primitive trailer");while(q.p<q.end)if(q.u8())q.fail("nonzero primitive padding");return s;}
std::vector<std::array<std::int16_t,4>> triRecords(Reader& r,std::size_t n,std::size_t vertices,bool validate){auto span=r.array(n,8);r.p=span.offset;std::vector<std::array<std::int16_t,4>> v;v.reserve(n);for(std::size_t j=0;j<n;++j){std::array<std::int16_t,4>a{};for(auto& x:a)x=r.i16();if(validate)for(int k=0;k<3;++k)if(a[k]<0 || std::size_t(a[k])>=vertices)r.fail("triangle index outside vertices");v.push_back(a);}return v;}
int components(unsigned t,unsigned sub){switch(t){case 1:return sub==1?3:sub==2?4:sub==15?10:0;case 2:return sub==1?3:sub==2?4:sub==15?7:0;case 0x30:case 0x39:case 0x40:case 0x42:return 2;case 0x41:return 3;case 0x38:case 0x61:case 0xf1:return 10;case 0xf2:return 1;case 0x10:case 0x20:case 0x21:case 0x31:case 0x50:case 0x60:case 0x70:case 0x71:case 0x72:case 0x73:case 0x80:case 0x81:case 0x90:return 4;default:return 0;}}
}
Bytes readFile(const std::string& path){std::ifstream f(path,std::ios::binary);if(!f)throw FormatError("cannot open "+path);return Bytes(std::istreambuf_iterator<char>(f),{});}
Dvs parseDvs(const Bytes& data){
    Reader r(data);Dvs out;out.source={0,data.size()};r.magic("DVS\0");r.take(12);auto materialEnd=std::size_t(r.u32())*4;out.materialCountWord=r.u32();auto materialCount=std::int16_t(out.materialCountWord&65535);
    if(materialCount<0 || materialEnd<24 || materialEnd>data.size())r.fail("material count/end");Reader m(data,24,materialEnd);
    for(int i=0;i<materialCount;++i){Material mat;mat.source.offset=m.p;mat.flags=m.u16();m.take(34);for(unsigned bit:{1u,2u,4u})if(mat.flags&bit){TextureReference ref;ref.source={m.p,36};ref.roleBit=bit;m.take(4);ref.name=name(m,32);mat.textures.push_back(std::move(ref));}mat.source.bytes=m.p-mat.source.offset;out.materials.push_back(std::move(mat));}
    out.materialPadding=m.take(m.end-m.p);r.p=materialEnd;out.bounds=bounds(r);auto nodeCount=r.i16();r.take(2);for(auto& x:out.nodeHeaderShorts)x=r.i16();if(nodeCount<0 || std::size_t(nodeCount)>(r.end-r.p)/6)r.fail("node count");
    for(int i=0;i<nodeCount;++i){Node node;node.source.offset=r.p;auto end=std::size_t(r.u32())*4;node.type=r.i16();if(end<r.p || end>data.size())r.fail("node end");node.source.bytes=end-node.source.offset;Reader q(data,r.p,end);
        if(node.type>=0 && node.type<14 && node.type!=0 && node.type!=2 && node.type!=8 && node.type!=11){node.hasCommon=true;node.commonSource.offset=q.p;auto cr=q.take(74);Reader c(data,cr.offset,cr.offset+cr.bytes);node.commonFlags=c.u16();c.p=cr.offset;for(auto& v:node.commonShorts)v=c.i16();for(auto& row:node.commonVectors)for(auto& v:row)v=c.f32();if(node.commonFlags&1)node.optionalMatrix=floats(q,16);node.commonSource.bytes=q.p-node.commonSource.offset;}
        if(node.type==1){node.hasGeometry=true;auto& g=node.geometry;g.source.offset=q.p;g.mode=q.u8();auto small=q.u8();g.smallSigned=std::int8_t(small<128?int(small):int(small)-256);g.flags=q.u16();auto mi=q.u8();g.materialIndex=mi==255?-1:mi;g.unknownByte=q.u8();auto n=q.i16();if(n<0 || (g.materialIndex>=0 && std::size_t(mi)>=out.materials.size()))q.fail("geometry vertex/material count");g.vertexCount=std::size_t(n);unsigned mf=mi==255?0:out.materials[mi].flags;
            if(g.flags&0x169){g.positionsSource.offset=q.p;g.positionScale=q.f32();auto span=q.array(g.vertexCount,6);q.p=span.offset;g.positions.reserve(g.vertexCount);g.rawPositions.reserve(g.vertexCount);for(std::size_t j=0;j<g.vertexCount;++j){std::array<std::int16_t,3> raw{};g.positions.push_back(xyz(q,g.positionScale/32767.0f,&raw));g.rawPositions.push_back(raw);}g.positionsSource.bytes=q.p-g.positionsSource.offset;q.align();}
            if(g.flags&0x149){if((g.flags&0x4000)||(mf&2)){auto span=q.array(g.vertexCount,3);q.p=span.offset;for(std::size_t j=0;j<g.vertexCount;++j){std::array<std::int8_t,3> v{};for(auto& x:v){auto b=q.u8();x=std::int8_t(b<128?int(b):int(b)-256);}g.normals.push_back(v);}q.align();}auto nc=g.flags&0x1000?g.vertexCount:1;auto span=q.array(nc,4);q.p=span.offset;for(std::size_t j=0;j<nc;++j){std::array<std::uint8_t,4> v{};for(auto& x:v)x=q.u8();g.rgba.push_back(v);}if(mf&5){q.array(g.vertexCount,8);q.p-=g.vertexCount*8;for(std::size_t j=0;j<g.vertexCount;++j){Vec2 uv;uv.x=q.f32();uv.y=q.f32();g.uv.push_back(uv);}}g.bounds=bounds(q);
                if(g.flags&4){auto nb=q.u32();if(nb>=21)q.fail("bone count reaches native early return");q.array(nb,68);q.p-=std::size_t(nb)*68;for(unsigned j=0;j<nb;++j){Bone b;b.source={q.p,68};b.idWord=q.u32();for(auto& v:b.matrix)v=q.f32();g.bones.push_back(b);}g.weightsSource=q.array(g.vertexCount,24);g.weightRecords.assign(data.begin()+g.weightsSource.offset,data.begin()+g.weightsSource.offset+g.weightsSource.bytes);}
                if(g.flags&0x2041){auto nt=q.u32();g.triangleRecords=triRecords(q,nt,g.vertexCount,true);g.secondaryCounts[0]=q.i16();g.secondaryCounts[1]=q.i16();if(g.secondaryCounts[0]<0 || g.secondaryCounts[1]<0)q.fail("negative secondary count");g.secondaryTriangleRecords=triRecords(q,std::size_t(g.secondaryCounts[0])+g.secondaryCounts[1],g.vertexCount,false);}
                auto ns=g.mode!=4 && (g.flags&0x2000)?3:1;for(int j=0;j<ns;++j)g.primitiveStreams.push_back(primitives(q,g.vertexCount));
            }g.source.bytes=q.p-g.source.offset;
        }else if(node.type==4){auto p=q.p;q.take(24);Reader c(data,p,p+24);c.take(2);auto flags=c.u16();auto value=c.i16();if(value)q.take(4);if(flags&2)q.take(32);
        }else if(node.type==5){auto value=q.i16();auto flags=q.u16();q.take(8);if(flags&0x30)q.take(4);if(value==3)q.take(8);else if(value==2)q.take(12);
        }else if(node.type==12 || node.type==13){auto p=q.p;q.take(12);node.typePayload.assign(data.begin()+p,data.begin()+p+12);if(node.type==12){Reader c(data,p,p+12);node.emitter.first=c.u16();node.emitter.second=c.u16();node.emitter.scalar0=c.f32();node.emitter.scalar1=c.f32();}else{Reader c(data,p+6,p+8);auto mi=c.i16();if(mi!=255 && (mi<0 || std::size_t(mi)>=out.materials.size()))q.fail("type13 material index");}
        }else if(node.type<0 || node.type>13)node.unsupportedType=true;
        node.uninterpreted=q.take(q.end-q.p);out.nodes.push_back(std::move(node));r.p=end;
    }out.postNodeTail=r.take(r.end-r.p);return out;
}
Dak parseDak(const Bytes& data){Reader r(data);Dak out;out.source={0,data.size()};r.magic("DAK\0");r.take(4);auto count=r.u32();out.flags=r.u32();out.durationWord=r.u32();if(count>(r.end-r.p)/8)r.fail("DAK object count");for(unsigned i=0;i<count;++i){DakObject obj;obj.source.offset=r.p;auto end=std::size_t(r.u32())*4;obj.targetNodeIndex=r.i16();obj.declaredSectionCount=r.i16();if(end<r.p || end>data.size() || obj.declaredSectionCount<0)r.fail("DAK object bounds/count");Reader q(data,r.p,end);if(obj.targetNodeIndex>=0)obj.transform=floats(q,16);for(int j=0;j<obj.declaredSectionCount;++j){DakSection s;s.source.offset=q.p;s.type=q.u8();s.subtype=q.u8();s.count=q.i16();if(s.count<0)q.fail("DAK negative key count");if(s.type==0x10)s.prefix=q.take(4);if(s.type==1){s.prefix=q.take(16);}s.components=components(s.type,s.subtype);if(!s.components){s.source.bytes=q.p-s.source.offset;obj.sections.push_back(std::move(s));break;}s.supported=true;s.payload=q.array(std::size_t(s.count),std::size_t(s.components)*4);Reader keys(data,s.payload.offset,s.payload.offset+s.payload.bytes);s.keyWords.reserve(s.payload.bytes/4);while(keys.p<keys.end)s.keyWords.push_back(keys.u32());if(s.subtype==11)s.proceduralTrailer=q.take(s.components==4?12:20);s.source.bytes=q.p-s.source.offset;obj.sections.push_back(std::move(s));}obj.uninterpreted=q.take(q.end-q.p);obj.source.bytes=end-obj.source.offset;out.objects.push_back(std::move(obj));r.p=end;}out.postObjectTail=r.take(r.end-r.p);return out;}
Asc parseAsc(const Bytes& data){
    Reader r(data);Asc out;out.source={0,data.size()};r.magic("ASC\0");for(auto& x:out.headerWords)x=r.u32();
    Reader a(data,out.headerWords[0],data.size());a.magic("AFT\0");auto delta=a.u32();auto size=a.u32();a.u32();
    auto start=std::size_t(out.headerWords[0])+delta;Reader n(data,start,data.size());out.nameTable=n.take(size);Reader names(data,start,start+size);
    while(names.p<names.end){AscName value;value.offset=names.p;bool terminated=false;while(names.p<names.end){auto c=names.u8();if(!c){terminated=true;break;}if(c>=128)names.fail("non-ASCII AFT name");value.name.push_back(char(c));}if(!terminated)names.fail("unterminated AFT name");if(!value.name.empty())out.names.push_back(std::move(value));}
    if(out.names.size()!=out.headerWords[1])r.fail("ASC AFT name count");
    r.p=start+size;if(out.headerWords[2]>(r.end-r.p)/16)r.fail("ASC ASQ block count");
    for(std::uint32_t i=0;i<out.headerWords[2];++i){AscBlock block;block.source.offset=r.p;r.magic("ASQ\0");auto headerBytes=r.u32();auto payloadBytes=r.u32();block.opaqueWord=r.u32();if(headerBytes!=16 || payloadBytes%4)r.fail("unsupported ASQ header/payload alignment");block.payload=r.take(payloadBytes);Reader words(data,block.payload.offset,block.payload.offset+block.payload.bytes);while(words.p<words.end)block.words.push_back(words.u32());block.source.bytes=r.p-block.source.offset;out.blocks.push_back(std::move(block));}
    out.postBlockTail=r.take(r.end-r.p);return out;
}
void validateBinding(const Dvs& model,const Dak& animation){for(const auto& object:animation.objects)if(object.targetNodeIndex>=0 && std::size_t(object.targetNodeIndex)>=model.nodes.size())throw FormatError("DAK target outside DVS node table");}
std::vector<std::array<std::uint16_t,3>> triangles(const Geometry& g,std::size_t streamIndex){std::vector<std::array<std::uint16_t,3>> out;if(streamIndex>=g.primitiveStreams.size())return out;for(const auto& p:g.primitiveStreams[streamIndex].primitives){const auto& v=p.indices;auto push=[&](std::uint16_t a,std::uint16_t b,std::uint16_t c){if(a!=b && a!=c && b!=c)out.push_back({a,b,c});};if(p.topology==Topology::TriangleStrip)for(std::size_t i=2;i<v.size();++i){if(i%2)push(v[i-1],v[i-2],v[i]);else push(v[i-2],v[i-1],v[i]);}else if(p.topology==Topology::Triangles)for(std::size_t i=2;i<v.size();i+=3)push(v[i-2],v[i-1],v[i]);else if(p.topology==Topology::TriangleFan)for(std::size_t i=2;i<v.size();++i)push(v[0],v[i-1],v[i]);}return out;}
} // namespace dokodemo::model
