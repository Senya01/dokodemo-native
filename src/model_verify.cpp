// Headless original-data verifier; intentionally separate from the shared GUI.
#ifdef DOKODEMO_MODEL_VERIFY_MAIN
#include "model_formats.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace dokodemo::model;
namespace {
void put32(Bytes& b,std::size_t at,std::uint32_t v){if(at+4>b.size())throw FormatError("test mutation outside source");for(int i=0;i<4;++i)b[at+i]=std::uint8_t(v>>(8*i));}
void put16(Bytes& b,std::size_t at,std::uint16_t v){if(at+2>b.size())throw FormatError("test mutation outside source");b[at]=std::uint8_t(v);b[at+1]=std::uint8_t(v>>8);}
template<typename F> void rejects(const char* what,const Bytes& original,F parse,std::size_t offset,std::uint32_t value,bool word=true){Bytes bad=original;if(word)put32(bad,offset,value);else put16(bad,offset,std::uint16_t(value));try{parse(bad);}catch(const FormatError&){return;}throw FormatError(std::string("accepted malformed ")+what);}
void truncation(const Bytes& data){for(std::size_t n=0;n<20;++n){Bytes bad(data.begin(),data.begin()+std::min(n,data.size()));bool rejected=false;try{if(!std::memcmp(data.data(),"DVS",3))parseDvs(bad);else if(!std::memcmp(data.data(),"DAK",3))parseDak(bad);else parseAsc(bad);}catch(const FormatError&){rejected=true;}if(!rejected)throw FormatError("accepted truncated header");}}
void selfTest(const std::string& root){
    auto dvs=readFile(root+"/NEKO.BPM/NEKO/TORO.DVS");auto a=parseDvs(dvs);truncation(dvs);
    rejects("DVS material end",dvs,parseDvs,16,0xffffffff);rejects("DVS material count",dvs,parseDvs,20,0xffff,false);
    auto& first=a.nodes.front();rejects("DVS node end",dvs,parseDvs,first.source.offset,0xffffffff);rejects("DVS vertex count",dvs,parseDvs,first.geometry.source.offset+6,0xffff,false);rejects("DVS material index",dvs,parseDvs,first.geometry.source.offset+4,0x00fe,false);
    for(const auto& node:a.nodes)if(node.hasGeometry && !node.geometry.primitiveStreams.empty() && !node.geometry.primitiveStreams[0].primitives.empty()){
        const auto& p=node.geometry.primitiveStreams[0].primitives[0];rejects("DVS primitive vertex",dvs,parseDvs,p.source.offset+2,0x7fff,false);break;
    }
    auto dak=readFile(root+"/NEKO.BPM/NEKO/TORO_TALK09.DAK");auto d=parseDak(dak);truncation(dak);validateBinding(a,d);
    rejects("DAK object count",dak,parseDak,8,0xffffffff);rejects("DAK backward pointer",dak,parseDak,20,0);rejects("DAK outside pointer",dak,parseDak,20,0xffffffff);rejects("DAK section count",dak,parseDak,26,0xffff,false);
    auto asc=readFile(root+"/NEKO.BPM/NEKO/NEKO.ASC");auto s=parseAsc(asc);truncation(asc);rejects("ASC name bounds",asc,parseAsc,24,0xffffffff);rejects("ASC block count",asc,parseAsc,12,0xffffffff);rejects("ASC ASQ size",asc,parseAsc,s.blocks.front().source.offset+8,0xffffffff);
    if(a.nodes.size()!=22 || d.objects.size()!=17 || s.names.size()!=207 || s.blocks.size()!=1294)throw FormatError("original fixture count drift");
    std::cout<<"PASS 60 truncated headers, 13 malformed counts/offsets/indices, original DVS/DAK/ASC\n";
}
void report(const std::string& path){auto data=readFile(path);std::size_t objects=0,materials=0,vertices=0,faces=0,streams=0,unsupported=0,residual=0;double sum=0;std::string type;
    if(data.size()>=4 && !std::memcmp(data.data(),"DVS\0",4)){type="DVS";auto a=parseDvs(data);objects=a.nodes.size();materials=a.materials.size();residual=a.postNodeTail.bytes;for(const auto& n:a.nodes){residual+=n.uninterpreted.bytes;if(n.unsupportedType)++unsupported;if(n.hasGeometry){const auto& g=n.geometry;vertices+=g.vertexCount;faces+=triangles(g).size();streams+=g.primitiveStreams.size();for(const auto& v:g.positions)sum+=v.x+v.y+v.z;}}}
    else if(data.size()>=4 && !std::memcmp(data.data(),"DAK\0",4)){type="DAK";auto a=parseDak(data);objects=a.objects.size();residual=a.postObjectTail.bytes;for(const auto& o:a.objects){residual+=o.uninterpreted.bytes;for(const auto& s:o.sections){++streams;if(!s.supported)++unsupported;}}}
    else if(data.size()>=4 && !std::memcmp(data.data(),"ASC\0",4)){type="ASC";auto a=parseAsc(data);objects=a.blocks.size();materials=a.names.size();streams=a.blocks.size();residual=a.postBlockTail.bytes;}
    else throw FormatError("unknown verifier input");
    std::cout<<path<<'\t'<<type<<'\t'<<objects<<'\t'<<materials<<'\t'<<vertices<<'\t'<<faces<<'\t'<<streams<<'\t'<<unsupported<<'\t'<<residual<<'\t'<<std::setprecision(12)<<sum<<'\n';
}
}
int main(int argc,char** argv){try{if(argc==3 && std::string(argv[1])=="--self-test"){selfTest(argv[2]);return 0;}if(argc==3 && std::string(argv[1])=="--list"){std::ifstream input(argv[2]);if(!input)throw FormatError("cannot open list");std::string path;while(std::getline(input,path)){if(!path.empty() && path.back()=='\r')path.pop_back();if(!path.empty())report(path);}return 0;}if(argc==2){report(argv[1]);return 0;}std::cerr<<"model-verify [file | --list paths.txt | --self-test source-directory]\n";return 2;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
