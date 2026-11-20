#include "room_meeting_crypto_win32.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <utility>

namespace dokodemo::platform {
namespace {
struct Algorithm {
    BCRYPT_ALG_HANDLE handle{};
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle,0); }
};

struct Key {
    BCRYPT_KEY_HANDLE handle{};
    ~Key() { if (handle) BCryptDestroyKey(handle); }
};

struct Secret {
    BCRYPT_SECRET_HANDLE handle{};
    ~Secret() { if (handle) BCryptDestroySecret(handle); }
};

struct KeyMaterial {
    std::array<std::uint8_t,32> bytes{};
    ~KeyMaterial() { SecureZeroMemory(bytes.data(),bytes.size()); }
};

bool Ok(NTSTATUS status) { return status>=0; }
} // namespace

struct MeetingCipher::Data {
    Algorithm exchange, aes;
    Key local, symmetric;
};

MeetingCipher::MeetingCipher()=default;
MeetingCipher::~MeetingCipher()=default;

bool MeetingCipher::Generate() {
    auto next=std::make_unique<Data>();
    PublicKey blob{};
    ULONG written=0;
    if (!Ok(BCryptOpenAlgorithmProvider(&next->exchange.handle,BCRYPT_ECDH_P256_ALGORITHM,nullptr,0)) ||
        !Ok(BCryptGenerateKeyPair(next->exchange.handle,&next->local.handle,256,0)) ||
        !Ok(BCryptFinalizeKeyPair(next->local.handle,0)) ||
        !Ok(BCryptExportKey(next->local.handle,nullptr,BCRYPT_ECCPUBLIC_BLOB,
            blob.data(),ULONG(blob.size()),&written,0)) || written!=blob.size())
        return false;
    public_key_=blob;
    data_=std::move(next);
    return true;
}

bool MeetingCipher::Agree(const PublicKey& peer,bool client,const std::uint8_t* client_hello,
                          const std::uint8_t* server_hello,std::size_t hello_size) {
    if (!data_ || !data_->local.handle || data_->symmetric.handle || peer==public_key_ ||
        !client_hello || !server_hello || !hello_size || hello_size>512)
        return false;
    auto published=peer;
    Key peer_key;
    Secret secret;
    if (!Ok(BCryptImportKeyPair(data_->exchange.handle,nullptr,BCRYPT_ECCPUBLIC_BLOB,
                               &peer_key.handle,published.data(),ULONG(published.size()),0)) ||
        !Ok(BCryptSecretAgreement(data_->local.handle,peer_key.handle,&secret.handle,0)))
        return false;
    // Windows CNG computes SHA-256 over the ordered greeting, the ECDH
    // secret, then the second greeting. The greetings include protocol
    // version, mode, venue, both session identifiers and ephemeral keys.
    BCryptBuffer controls[]{
        {sizeof(BCRYPT_SHA256_ALGORITHM),KDF_HASH_ALGORITHM,const_cast<wchar_t*>(BCRYPT_SHA256_ALGORITHM)},
        {ULONG(hello_size),KDF_SECRET_PREPEND,const_cast<std::uint8_t*>(client_hello)},
        {ULONG(hello_size),KDF_SECRET_APPEND,const_cast<std::uint8_t*>(server_hello)},
    };
    BCryptBufferDesc options{BCRYPTBUFFER_VERSION,ULONG(std::size(controls)),controls};
    KeyMaterial material;
    ULONG written=0;
    if (!Ok(BCryptDeriveKey(secret.handle,BCRYPT_KDF_HASH,&options,material.bytes.data(),
                           ULONG(material.bytes.size()),&written,0)) || written!=material.bytes.size() ||
        !Ok(BCryptOpenAlgorithmProvider(&data_->aes.handle,BCRYPT_AES_ALGORITHM,nullptr,0)) ||
        !Ok(BCryptSetProperty(data_->aes.handle,BCRYPT_CHAINING_MODE,
                            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                            sizeof(BCRYPT_CHAIN_MODE_GCM),0)) ||
        !Ok(BCryptGenerateSymmetricKey(data_->aes.handle,&data_->symmetric.handle,nullptr,0,
                                      material.bytes.data(),ULONG(material.bytes.size()),0)))
        return false;
    client_=client;
    return true;
}

std::optional<std::vector<std::uint8_t>> MeetingCipher::Seal(std::uint64_t sequence,
                                                             const std::uint8_t* data,std::size_t size) const {
    return Transform(true,sequence,data,size);
}

std::optional<std::vector<std::uint8_t>> MeetingCipher::Open(std::uint64_t sequence,
                                                             const std::uint8_t* data,std::size_t size) const {
    return Transform(false,sequence,data,size);
}

std::optional<std::vector<std::uint8_t>> MeetingCipher::Transform(bool encrypt,std::uint64_t sequence,
                                                                  const std::uint8_t* data,std::size_t size) const {
    if (!data_ || !data_->symmetric.handle || !data || size>4096 || (encrypt ? !size : size<=tag_size))
        return {};
    const auto length=encrypt ? size : size-tag_size;
    std::vector<std::uint8_t> output(encrypt ? length+tag_size : length);
    std::array<std::uint8_t,tag_size> tag{};
    if (!encrypt) std::copy_n(data+length,tag_size,tag.begin());

    // Bytes 0..3 separate the client and server nonce spaces. The
    // transport serializes and advances the remaining 64 bits per frame.
    std::array<std::uint8_t,12> nonce{};
    nonce[0]=(encrypt==client_) ? 1 : 2;
    for (unsigned i=0;i<8;++i) nonce[4+i]=std::uint8_t(sequence>>(8*i));
    std::array<std::uint8_t,2> size_aad{std::uint8_t(length>>8),std::uint8_t(length)};
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce=nonce.data();
    info.cbNonce=ULONG(nonce.size());
    info.pbAuthData=size_aad.data();
    info.cbAuthData=ULONG(size_aad.size());
    info.pbTag=tag.data();
    info.cbTag=ULONG(tag.size());

    const auto operation=encrypt ? BCryptEncrypt : BCryptDecrypt;
    ULONG written=0;
    if (!Ok(operation(data_->symmetric.handle,const_cast<PUCHAR>(data),ULONG(length),&info,
                      nullptr,0,output.data(),ULONG(length),&written,0)) || written!=length)
        return {};
    if (encrypt) std::copy(tag.begin(),tag.end(),output.begin()+length);
    return output;
}
} // namespace dokodemo::platform
