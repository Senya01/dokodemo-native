#include "room_meeting_win32.h"
#include "room_meeting_crypto_win32.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <utility>

namespace dokodemo::platform {
namespace {
using gameplay::MeetingConnection;
using gameplay::MeetingPlace;
using Time = std::chrono::steady_clock;
using Identity = std::array<std::uint8_t,16>;

constexpr std::array<std::uint8_t,4> magic{'D','K','M','2'};
constexpr std::uint32_t multicast_address=0xefff294b; // 239.255.41.75
constexpr auto observe_interval=std::chrono::milliseconds(250);
constexpr auto handshake_time=std::chrono::seconds(3);
constexpr std::size_t protocol_fields=6;
using Advertisement = std::array<std::uint8_t,protocol_fields+16+2>;
constexpr std::size_t key_offset=protocol_fields+16+16;
using Handshake = std::array<std::uint8_t,key_offset+MeetingCipher::PublicKey{}.size()>;
constexpr std::uint8_t ready_marker=0xa5;
constexpr std::size_t proof_size=1+MeetingCipher::tag_size;

class Socket {
public:
    Socket() = default;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    ~Socket() { Reset(); }
    void Reset(SOCKET value=INVALID_SOCKET) {
        if (value_!=INVALID_SOCKET) closesocket(value_);
        value_=value;
    }
    SOCKET Get() const { return value_; }
    explicit operator bool() const { return value_!=INVALID_SOCKET; }
private:
    SOCKET value_=INVALID_SOCKET;
};

sockaddr_in Address(std::uint32_t network_address,std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=network_address;
    address.sin_port=htons(port);
    return address;
}

template<class T>
bool SetOption(SOCKET socket,int level,int option,const T& value) {
    return setsockopt(socket,level,option,reinterpret_cast<const char*>(&value),sizeof(value))==0;
}

bool Nonblocking(SOCKET socket) {
    u_long enabled=1;
    return ioctlsocket(socket,FIONBIO,&enabled)==0;
}

bool Retry() {
    const auto error=WSAGetLastError();
    return error==WSAEWOULDBLOCK || error==WSAEINTR;
}

enum class Io { active, complete, closed };

Io SendBytes(SOCKET socket,const std::uint8_t* data,std::size_t size,std::size_t& sent) {
    if (sent==size) return Io::complete;
    const int count=send(socket,reinterpret_cast<const char*>(data+sent),int(size-sent),0);
    if (count==SOCKET_ERROR) return Retry()?Io::active:Io::closed;
    if (!count) return Io::closed;
    sent+=unsigned(count);
    return sent==size?Io::complete:Io::active;
}

Io ReadBytes(SOCKET socket,std::uint8_t* data,std::size_t size,std::size_t& read) {
    if (read==size) return Io::complete;
    const int count=recv(socket,reinterpret_cast<char*>(data+read),int(size-read),0);
    if (count==SOCKET_ERROR) return Retry()?Io::active:Io::closed;
    if (!count) return Io::closed;
    read+=unsigned(count);
    return read==size?Io::complete:Io::active;
}

class LanMeeting final : public gameplay::RoomMeetingChannel {
public:
    LanMeeting(MeetingPlace place,std::uint16_t port,bool loopback) : place_(place) {
        WSADATA data{};
        provider_=WSAStartup(MAKEWORD(2,2),&data)==0;
        if (!provider_ || data.wVersion!=MAKEWORD(2,2) || !port || place_.venue>=4 ||
            static_cast<unsigned>(place_.mode)>2 ||
            BCryptGenRandom(nullptr,identity_.data(),ULONG(identity_.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
            return;

        const auto interface_addr=htonl(loopback?INADDR_LOOPBACK:INADDR_ANY);
        listener_.Reset(socket(AF_INET,SOCK_STREAM,IPPROTO_TCP));
        discovery_.Reset(socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP));
        if (!listener_ || !discovery_ || !Nonblocking(listener_.Get()) || !Nonblocking(discovery_.Get()))
            return;

        const BOOL yes=TRUE;
        auto server=Address(interface_addr,0);
        int length=sizeof(server);
        if (!SetOption(listener_.Get(),SOL_SOCKET,SO_EXCLUSIVEADDRUSE,yes) ||
            bind(listener_.Get(),reinterpret_cast<const sockaddr*>(&server),length)!=0 ||
            listen(listener_.Get(),2)!=0 ||
            getsockname(listener_.Get(),reinterpret_cast<sockaddr*>(&server),&length)!=0 ||
            !SetOption(discovery_.Get(),SOL_SOCKET,SO_REUSEADDR,yes))
            return;

        group_=Address(htonl(multicast_address),port);
        const auto any=Address(htonl(INADDR_ANY),port);
        ip_mreq membership{};
        membership.imr_multiaddr=group_.sin_addr;
        membership.imr_interface.s_addr=interface_addr;
        const DWORD one=1;
        if (bind(discovery_.Get(),reinterpret_cast<const sockaddr*>(&any),sizeof(any))!=0 ||
            !SetOption(discovery_.Get(),IPPROTO_IP,IP_ADD_MEMBERSHIP,membership) ||
            !SetOption(discovery_.Get(),IPPROTO_IP,IP_MULTICAST_IF,interface_addr) ||
            !SetOption(discovery_.Get(),IPPROTO_IP,IP_MULTICAST_TTL,one) ||
            !SetOption(discovery_.Get(),IPPROTO_IP,IP_MULTICAST_LOOP,one))
            return;
        FillIdentity(advertisement_);
        const auto listen_port=ntohs(server.sin_port);
        advertisement_[protocol_fields+16]=std::uint8_t(listen_port>>8);
        advertisement_[protocol_fields+17]=std::uint8_t(listen_port);
        state_=MeetingConnection::looking;
    }
    ~LanMeeting() override {
        connection_.Reset();
        listener_.Reset();
        discovery_.Reset();
        if (provider_) WSACleanup();
    }

    MeetingConnection Poll() override {
        if (state_==MeetingConnection::connected) {
            if (!queued_.empty()) {
                const auto io=SendBytes(connection_.Get(),queued_.data(),queued_.size(),queued_bytes_);
                if (io==Io::closed) Close();
                if (io==Io::complete) queued_.clear();
            }
            if (state_==MeetingConnection::connected && !incoming_) ReadMessage();
        } else if (state_==MeetingConnection::looking) {
            const auto now=Time::now();
            if (now>=next_advertisement_) {
                sendto(discovery_.Get(),reinterpret_cast<const char*>(advertisement_.data()),
                       int(advertisement_.size()),0,reinterpret_cast<const sockaddr*>(&group_),sizeof(group_));
                next_advertisement_=now+observe_interval;
            }
            if (!connection_) FindPeer(now);
            if (connection_) AdvanceHandshake(now);
        }
        return state_;
    }

    bool Send(const std::vector<std::uint8_t>& message) override {
        if (state_!=MeetingConnection::connected || !queued_.empty() || message.empty() ||
            message.size()>max_message) return false;
        if (send_sequence_==(std::numeric_limits<std::uint64_t>::max)()) { Close(); return false; }
        const auto sealed=cipher_->Seal(send_sequence_,message.data(),message.size());
        if (!sealed) { Close(); return false; }
        queued_={std::uint8_t(sealed->size()>>8),std::uint8_t(sealed->size())};
        queued_.insert(queued_.end(),sealed->begin(),sealed->end());
        ++send_sequence_;
        queued_bytes_=0;
        return true;
    }

    bool FirstParticipant() const override { return initiated_; }
    bool HasPendingOutput() const override { return !queued_.empty(); }

    std::optional<std::vector<std::uint8_t>> Receive() override {
        return std::exchange(incoming_,std::nullopt);
    }
private:
    template<std::size_t N> void FillIdentity(std::array<std::uint8_t,N>& message) const {
        std::copy(magic.begin(),magic.end(),message.begin());
        message[4]=static_cast<std::uint8_t>(place_.mode);
        message[5]=place_.venue;
        std::copy(identity_.begin(),identity_.end(),message.begin()+protocol_fields);
    }
    template<std::size_t N> bool Compatible(const std::array<std::uint8_t,N>& message) const {
        return std::equal(magic.begin(),magic.end(),message.begin()) &&
               message[4]==static_cast<std::uint8_t>(place_.mode) && message[5]==place_.venue;
    }
    template<std::size_t N> static Identity Sender(const std::array<std::uint8_t,N>& message) {
        Identity peer{};
        std::copy_n(message.begin()+protocol_fields,peer.size(),peer.begin());
        return peer;
    }
    void BeginHandshake(SOCKET socket,Time::time_point now,bool initiator) {
        connection_.Reset(socket);
        initiated_=initiator;
        connecting_=false;
        peer_validated_=false;
        reply_={};
        received_=sent_=proof_read_=proof_written_=0;
        proof_.clear();
        expires_=now+handshake_time;
        cipher_=std::make_unique<MeetingCipher>();
        if (!cipher_->Generate()) {
            connection_.Reset();
            state_=MeetingConnection::unavailable;
            return;
        }
        if (initiated_) PrepareHello();
    }
    void PrepareHello() {
        FillIdentity(hello_);
        std::copy(peer_.begin(),peer_.end(),hello_.begin()+protocol_fields+peer_.size());
        const auto& key=cipher_->public_key();
        std::copy(key.begin(),key.end(),hello_.begin()+key_offset);
    }
    void Close() {
        connection_.Reset();
        state_=MeetingConnection::disconnected;
        cipher_.reset();
    }
    void FindPeer(Time::time_point now) {
        const auto socket=accept(listener_.Get(),nullptr,nullptr);
        if (socket!=INVALID_SOCKET) {
            BeginHandshake(socket,now,false);
            if (connection_ && !Nonblocking(socket)) connection_.Reset();
            return;
        }
        for (unsigned attempt=0;attempt<16;++attempt) {
            Advertisement message{};
            sockaddr_in from{};
            int length=sizeof(from);
            const int count=recvfrom(discovery_.Get(),reinterpret_cast<char*>(message.data()),
                                     int(message.size()),0,reinterpret_cast<sockaddr*>(&from),&length);
            if (count==SOCKET_ERROR && WSAGetLastError()!=WSAEMSGSIZE) return;
            if (count!=int(message.size()) || from.sin_family!=AF_INET || !Compatible(message)) continue;
            const auto other=Sender(message);
            if (!(identity_<other)) continue; // The smaller nonce initiates.
            const auto port=std::uint16_t((unsigned(message[protocol_fields+16])<<8)|
                                          message[protocol_fields+17]);
            if (!port) continue;
            from.sin_port=htons(port);
            const auto peer_socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            if (peer_socket==INVALID_SOCKET) return;
            peer_=other;
            BeginHandshake(peer_socket,now,true);
            if (!connection_ || !Nonblocking(peer_socket)) { connection_.Reset(); return; }
            if (connect(peer_socket,reinterpret_cast<const sockaddr*>(&from),sizeof(from))!=0) {
                if (WSAGetLastError()!=WSAEWOULDBLOCK) { connection_.Reset(); return; }
                connecting_=true;
            }
            return;
        }
    }
    void AdvanceHandshake(Time::time_point now) {
        const auto socket=connection_.Get();
        if (now>=expires_) { connection_.Reset(); return; }
        if (connecting_) {
            fd_set connected{}, errors{};
            FD_SET(socket,&connected);
            FD_SET(socket,&errors);
            timeval instant{};
            if (select(0,nullptr,&connected,&errors,&instant)==SOCKET_ERROR || FD_ISSET(socket,&errors)) {
                connection_.Reset();
                return;
            }
            if (!FD_ISSET(socket,&connected)) return;
            connecting_=false;
        }
        if (!peer_validated_) {
            const auto io=ReadBytes(socket,reply_.data(),reply_.size(),received_);
            if (io==Io::closed) { connection_.Reset(); return; }
            if (io==Io::complete) {
                const auto other=Sender(reply_);
                if (!Compatible(reply_) ||
                    !std::equal(identity_.begin(),identity_.end(),reply_.begin()+protocol_fields+identity_.size()) ||
                    (initiated_ ? peer_!=other : !(other<identity_))) {
                    connection_.Reset();
                    return;
                }
                peer_=other;
                peer_validated_=true;
                if (!initiated_) PrepareHello();
            }
        }
        if (initiated_ || peer_validated_) {
            const auto io=SendBytes(socket,hello_.data(),hello_.size(),sent_);
            if (io==Io::closed) { connection_.Reset(); return; }
        }
        if (peer_validated_ && sent_==hello_.size()) ConfirmKey();
    }
    void ConfirmKey() {
        if (proof_.empty()) {
            MeetingCipher::PublicKey key{};
            std::copy_n(reply_.begin()+key_offset,key.size(),key.begin());
            const auto& client=initiated_?hello_:reply_;
            const auto& server=initiated_?reply_:hello_;
            if (!cipher_->Agree(key,initiated_,client.data(),server.data(),client.size())) {
                connection_.Reset();
                return;
            }
            auto proof=cipher_->Seal(0,&ready_marker,1);
            if (!proof) { connection_.Reset(); return; }
            proof_=std::move(*proof);
        }
        const auto socket=connection_.Get();
        const auto sent=SendBytes(socket,proof_.data(),proof_.size(),proof_written_);
        const auto received=ReadBytes(socket,peer_proof_.data(),peer_proof_.size(),proof_read_);
        if (sent==Io::closed || received==Io::closed) { connection_.Reset(); return; }
        if (sent!=Io::complete || received!=Io::complete) return;
        const auto proof=cipher_->Open(0,peer_proof_.data(),peer_proof_.size());
        if (!proof || proof->size()!=1 || proof->front()!=ready_marker) { connection_.Reset(); return; }
        state_=MeetingConnection::connected;
        discovery_.Reset();
        listener_.Reset();
    }
    void ReadMessage() {
        const auto socket=connection_.Get();
        const auto header=ReadBytes(socket,size_bytes_.data(),size_bytes_.size(),size_read_);
        if (header==Io::closed) { Close(); return; }
        if (header!=Io::complete) return;
        const auto size=(unsigned(size_bytes_[0])<<8)|size_bytes_[1];
        if (size<=MeetingCipher::tag_size || size>message_.size() ||
            receive_sequence_==(std::numeric_limits<std::uint64_t>::max)()) { Close(); return; }
        const auto data=ReadBytes(socket,message_.data(),size,message_read_);
        if (data==Io::closed) { Close(); return; }
        if (data!=Io::complete) return;
        auto decoded=cipher_->Open(receive_sequence_,message_.data(),size);
        if (!decoded) { Close(); return; }
        incoming_=std::move(decoded);
        ++receive_sequence_;
        size_read_=message_read_=0;
    }

    MeetingPlace place_;
    MeetingConnection state_=MeetingConnection::unavailable;
    bool provider_=false, initiated_=false, connecting_=false, peer_validated_=false;
    Socket listener_, discovery_, connection_;
    sockaddr_in group_{};
    Identity identity_{}, peer_{};
    Advertisement advertisement_{};
    Handshake hello_{}, reply_{};
    std::unique_ptr<MeetingCipher> cipher_;
    std::vector<std::uint8_t> proof_;
    std::array<std::uint8_t,proof_size> peer_proof_{};
    Time::time_point expires_{}, next_advertisement_{};
    std::size_t sent_=0, received_=0, proof_read_=0, proof_written_=0;
    std::uint64_t send_sequence_=1, receive_sequence_=1;
    std::array<std::uint8_t,2> size_bytes_{};
    std::array<std::uint8_t,max_message+MeetingCipher::tag_size> message_{};
    std::size_t size_read_=0, message_read_=0, queued_bytes_=0;
    std::optional<std::vector<std::uint8_t>> incoming_;
    std::vector<std::uint8_t> queued_;
};
} // namespace

gameplay::OpenRoomMeeting MakeLanMeetingOpener(std::uint16_t port,bool loopback) {
    return [port,loopback](MeetingPlace place) {
        return std::make_unique<LanMeeting>(place,port,loopback);
    };
}
} // namespace dokodemo::platform
