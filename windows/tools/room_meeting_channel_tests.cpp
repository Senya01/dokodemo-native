#include "platform/room_meeting_win32.h"
#include "platform/room_meeting_crypto_win32.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace dokodemo::gameplay;
using namespace std::chrono_literals;

namespace {
void Require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void CheckProtection() {
    using dokodemo::platform::MeetingCipher;
    MeetingCipher client,server,foreign;
    Require(client.Generate() && server.Generate() && foreign.Generate(),"Windows ECDH could not create a key");
    const std::array<std::uint8_t,5> first{1,2,3,4,5};
    const std::array<std::uint8_t,5> second{6,7,8,9,10};
    Require(client.Agree(server.public_key(),true,first.data(),second.data(),first.size()) &&
            server.Agree(client.public_key(),false,first.data(),second.data(),first.size()),
            "Peers could not derive the same meeting session");
    Require(foreign.Agree(client.public_key(),false,first.data(),second.data(),first.size()),
            "Could not prepare a distinct peer for isolation test");
    const std::vector<std::uint8_t> text{7,1,4,6,2,5,8};
    auto wire=client.Seal(1,text.data(),text.size());
    const auto reply=server.Seal(1,text.data(),text.size());
    Require(wire && reply && *wire!=*reply &&
            server.Open(1,wire->data(),wire->size())==text &&
            client.Open(1,reply->data(),reply->size())==text,"Protected frames did not preserve direction and content");
    Require(!foreign.Open(1,wire->data(),wire->size()) &&
            !server.Open(2,wire->data(),wire->size()) &&
            !server.Open(1,wire->data(),wire->size()-1),"An invalid session, sequence or length authenticated");
    wire->back()^=1;
    Require(!server.Open(1,wire->data(),wire->size()),"Modified frame authenticated");
}
}

int main(int argc,char** argv) try {
    Require(argc==5,"Expected port, venue, mode, and participant");
    const auto port=std::stoi(argv[1]), venue=std::stoi(argv[2]), mode=std::stoi(argv[3]), side=std::stoi(argv[4]);
    const auto* audio=std::getenv("DOKODEMO_TEST_AUDIO");
    Require(audio && std::string(audio)=="null","Missing null audio environment");
    Require(port>0 && port<=65535 && venue>=0 && venue<=3 && mode>=0 && mode<=2 &&
            side>=0 && side<=2,"Invalid meeting fixture");
    CheckProtection();
    std::cout<<"Session key, distinct directions, wrong session and modified record checked\n";
    auto open=dokodemo::platform::MakeLanMeetingOpener(std::uint16_t(port),true);
    auto channel=open({MeetingMode(mode),std::uint8_t(venue)});
    auto state=channel->Poll();
    Require(state==MeetingConnection::looking && !channel->Send({1}) && !channel->Receive(),
            "Protocol must wait for another participant");
    const auto until=[&](auto predicate,auto timeout) {
        const auto start=std::chrono::steady_clock::now();
        while (!predicate()) {
            if (std::chrono::steady_clock::now()-start>=timeout) return false;
            std::this_thread::sleep_for(10ms);
            state=channel->Poll();
        }
        return true;
    };
    if (!side) {
        const bool changed=until([&]{return state!=MeetingConnection::looking;},1300ms);
        Require(!changed && !channel->Receive(),"An incompatible meeting joined");
        std::cout<<"No matched peer remained looking\n";
    } else {
        Require(until([&]{return state==MeetingConnection::connected;},6s),"Compatible peer was not discovered");
        Require(!channel->Send({}) &&
                !channel->Send(std::vector<std::uint8_t>(RoomMeetingChannel::max_message+1,0)),
                "Invalid plaintext payload queued");
        const std::vector<std::uint8_t> own(RoomMeetingChannel::max_message,std::uint8_t(side));
        const std::vector<std::uint8_t> peer(RoomMeetingChannel::max_message,std::uint8_t(3-side));
        Require(channel->Send(own) && !channel->Send(own),"A second packet replaced unsent bytes");
        std::optional<std::vector<std::uint8_t>> received;
        Require(until([&]{received=channel->Receive(); return bool(received);},4s) && *received==peer,
                "Peer packet was truncated, duplicated, or looped back");
        if (side==1) {
            Require(until([&]{return state==MeetingConnection::disconnected;},4s),
                    "Closing one endpoint did not reach its peer");
            Require(!channel->Send(own) && !channel->Receive(),"Disconnected meeting accepted a payload");
        }
        std::cout<<"Participant "<<side<<" received "<<received->size()<<" peer bytes\n";
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
