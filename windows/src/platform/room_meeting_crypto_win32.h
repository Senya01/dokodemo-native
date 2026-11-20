#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace dokodemo::platform {

// A short-lived key agreement bound to both meeting greetings. Authenticating
// a frame establishes its connection and direction, without assigning a real
// identity to the neighboring player.
class MeetingCipher {
public:
    static constexpr std::size_t tag_size=16;
    using PublicKey=std::array<std::uint8_t,72>;

    MeetingCipher();
    ~MeetingCipher();
    MeetingCipher(const MeetingCipher&)=delete;
    MeetingCipher& operator=(const MeetingCipher&)=delete;
    bool Generate();
    const PublicKey& public_key() const { return public_key_; }
    bool Agree(const PublicKey& peer,bool client,const std::uint8_t* client_hello,
               const std::uint8_t* server_hello,std::size_t hello_size);
    std::optional<std::vector<std::uint8_t>> Seal(std::uint64_t sequence,
                                                 const std::uint8_t* data,std::size_t size) const;
    std::optional<std::vector<std::uint8_t>> Open(std::uint64_t sequence,
                                                 const std::uint8_t* data,std::size_t size) const;
private:
    struct Data;
    std::unique_ptr<Data> data_;
    PublicKey public_key_{};
    bool client_=false;
    std::optional<std::vector<std::uint8_t>> Transform(bool encrypt,std::uint64_t sequence,
                                                       const std::uint8_t* data,std::size_t size) const;
};

} // namespace dokodemo::platform
