#include "retail_cipher.h"
#include <windows.h>
#include <bcrypt.h>
#include <limits>
#include <stdexcept>

namespace dokodemo::platform {
namespace {
struct AesProvider {
    BCRYPT_ALG_HANDLE value{};
    ~AesProvider() { if (value) BCryptCloseAlgorithmProvider(value,0); }
};
struct AesKey {
    BCRYPT_KEY_HANDLE value{};
    ~AesKey() { if (value) BCryptDestroyKey(value); }
};
ULONG Size(std::size_t size) {
    if (size > (std::numeric_limits<ULONG>::max)()) throw std::length_error("CNG input too long");
    return static_cast<ULONG>(size);
}
void Check(NTSTATUS status) {
    if (status < 0) throw std::runtime_error("Windows savedata cipher service failed");
}
}
void RetailRandomBytes(std::uint8_t* out,std::size_t size) {
    Check(BCryptGenRandom(nullptr,out,Size(size),BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}
void RetailAesCbc(const AesBlock& key,bool encrypt,const std::uint8_t* input,std::uint8_t* output,std::size_t length) {
    AesProvider provider;
    Check(BCryptOpenAlgorithmProvider(&provider.value,BCRYPT_AES_ALGORITHM,nullptr,0));
    Check(BCryptSetProperty(provider.value,BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),sizeof(BCRYPT_CHAIN_MODE_CBC),0));
    AesKey handle;
    auto material=key;
    Check(BCryptGenerateSymmetricKey(provider.value,&handle.value,nullptr,0,material.data(),Size(material.size()),0));
    AesBlock iv{};
    ULONG written=0;
    auto operation=encrypt ? BCryptEncrypt : BCryptDecrypt;
    Check(operation(handle.value,const_cast<PUCHAR>(input),Size(length),nullptr,iv.data(),Size(iv.size()),
                    output,Size(length),&written,0));
    if (written!=length) throw std::runtime_error("Incomplete savedata AES result");
}
}
