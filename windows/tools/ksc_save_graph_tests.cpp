#include "gameplay/ksc_save_graph.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace dokodemo::gameplay;
int main(int argc,char** argv) {
    try {
        if (argc==3 && std::string(argv[1])=="--retail-buffer") {
            std::ifstream file(argv[2],std::ios::binary);
            if (!file) throw std::runtime_error("Cannot open retail clear buffer");
            const std::vector<std::uint8_t> buffer((std::istreambuf_iterator<char>(file)),{});
            if (buffer.size()!=0x40000 || buffer[0]!=0x6a || buffer[1]!=0x07 ||
                buffer[2]!=0x11 || buffer[3]!=0)
                throw std::runtime_error("Wrong clear buffer size or signature");
            std::size_t used=0;
            const auto graph=DecodeKscSaveGraph(buffer.data()+0x10b50,buffer.size()-0x10b50,used);
            const auto bytes=EncodeKscSaveGraph(graph);
            if (graph.children.size()!=27 || bytes.size()!=used ||
                !std::equal(bytes.begin(),bytes.end(),buffer.begin()+0x10b50))
                throw std::runtime_error("Retail graph round trip differs");
            std::cout<<"retail_vm_fields="<<graph.children.size()<<" used_bytes="<<used<<"\n";
            return 0;
        }
        if (argc != 1) throw std::runtime_error("Unexpected argument");
        // Source layout: three slots, an unaligned nested object following a
        // two-byte encoded string and its NUL, another string owned by that object.
        const std::vector<std::uint8_t> expected={
            3,0,0,0x90, 42,0,0,0x20, 16,0,0,0x60, 19,0,0,0xb0,
            0x82,0xa0,0, 2,0,0,0x90, 31,0,0,0x60, 0,0,0,0, 'X',0
        };
        const KscSaveValue nested{0x90000002,{},{{0x50000000,"X",{}},{}}};
        const KscSaveValue root{0x90000003,{},{{0x2000002a,{},{}},{0x60000000,"\x82\xa0",{}},
                                                 {0xb0000000,{},{nested}}}};
        if (EncodeKscSaveGraph(root)!=expected) throw std::runtime_error("Unexpected packed bytes");
        auto padded=expected;
        padded.resize(128);
        std::size_t used=0;
        auto value=DecodeKscSaveGraph(padded.data(),padded.size(),used);
        if (used!=expected.size() || EncodeKscSaveGraph(value)!=expected)
            throw std::runtime_error("Padding or nested reference changed the graph");
        for (std::size_t cut=0;cut<expected.size();++cut) {
            bool rejected=false;
            try { DecodeKscSaveGraph(expected.data(),cut,used); }
            catch(const std::invalid_argument&) { rejected=true; }
            if (!rejected || used!=expected.size()) throw std::runtime_error("Truncated graph was accepted");
        }
        padded[12]=0;
        try { DecodeKscSaveGraph(padded.data(),padded.size(),used); }
        catch(const std::invalid_argument&) { return 0; }
        throw std::runtime_error("Invalid reference was accepted");
    } catch(const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
