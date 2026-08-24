#include "lvgl_platform/crypto_provider.h"

#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace lvgl_platform {
namespace {

constexpr unsigned char kEmptyMessage = 0;

struct EVP_MD_CTX;
struct EVP_MD;
struct EVP_PKEY;
struct EVP_PKEY_CTX;

using MdCtxNew = EVP_MD_CTX* (*)();
using MdCtxFree = void (*)(EVP_MD_CTX*);
using DigestType = const EVP_MD* (*)();
using DigestInit = int (*)(EVP_MD_CTX*, const EVP_MD*, void*);
using DigestUpdate = int (*)(EVP_MD_CTX*, const void*, std::size_t);
using DigestFinal = int (*)(EVP_MD_CTX*, unsigned char*, unsigned int*);
using RawPublicKey = EVP_PKEY* (*)(int, void*, const unsigned char*, std::size_t);
using PkeyFree = void (*)(EVP_PKEY*);
using VerifyInit = int (*)(EVP_MD_CTX*, EVP_PKEY_CTX**, const EVP_MD*, void*, EVP_PKEY*);
using Verify = int (*)(
    EVP_MD_CTX*, const unsigned char*, std::size_t, const unsigned char*, std::size_t);

constexpr int kNidEd25519 = 1087;

#if defined(_WIN32)
using LibraryHandle = HMODULE;

LibraryHandle open_library() noexcept
{
    constexpr const char* names[] = {
        "libcrypto-3-x64.dll", "libcrypto-3.dll", "libcrypto-1_1-x64.dll",
        "libcrypto-1_1.dll"};
    for(const auto* name : names) {
        if(auto handle = ::LoadLibraryA(name)) return handle;
    }
    return nullptr;
}

void* resolve_symbol(LibraryHandle handle, const char* name) noexcept
{
    return reinterpret_cast<void*>(::GetProcAddress(handle, name));
}

#else
using LibraryHandle = void*;

LibraryHandle open_library() noexcept
{
    constexpr const char* names[] = {
        "libcrypto.so.3", "libcrypto.so.1.1", "libcrypto.so"};
    for(const auto* name : names) {
        if(auto* handle = ::dlopen(name, RTLD_NOW | RTLD_LOCAL)) return handle;
    }
    return nullptr;
}

void* resolve_symbol(LibraryHandle handle, const char* name) noexcept
{
    return ::dlsym(handle, name);
}

#endif

LibraryHandle process_crypto_library() noexcept
{
    // OpenSSL maintains process-global provider state. Unloading and reopening
    // libcrypto between verifier instances is unsafe on supported OpenSSL 3
    // builds, so pin the first successfully resolved module for process life.
    static const LibraryHandle handle = open_library();
    return handle;
}

template <typename Function>
Function load_function(LibraryHandle handle, const char* name) noexcept
{
    return reinterpret_cast<Function>(resolve_symbol(handle, name));
}

}  // namespace

struct CryptoProvider::Impl {
    LibraryHandle handle {nullptr};
    MdCtxNew md_ctx_new {nullptr};
    MdCtxFree md_ctx_free {nullptr};
    DigestType sha256 {nullptr};
    DigestType sha512 {nullptr};
    DigestInit digest_init {nullptr};
    DigestUpdate digest_update {nullptr};
    DigestFinal digest_final {nullptr};
    RawPublicKey raw_public_key {nullptr};
    PkeyFree pkey_free {nullptr};
    VerifyInit verify_init {nullptr};
    Verify verify {nullptr};

    bool complete() const noexcept
    {
        return handle != nullptr && md_ctx_new != nullptr && md_ctx_free != nullptr &&
               sha256 != nullptr && sha512 != nullptr && digest_init != nullptr &&
               digest_update != nullptr && digest_final != nullptr && raw_public_key != nullptr &&
               pkey_free != nullptr && verify_init != nullptr && verify != nullptr;
    }

    template <std::size_t Size>
    bool digest(
        DigestType type, const void* data, std::size_t size,
        std::array<std::uint8_t, Size>& output) const noexcept
    {
        if((data == nullptr && size != 0) || type == nullptr) return false;
        auto* context = md_ctx_new();
        if(context == nullptr) return false;
        const bool initialized = digest_init(context, type(), nullptr) == 1;
        const void* input = data == nullptr ? &kEmptyMessage : data;
        const bool updated = initialized && digest_update(context, input, size) == 1;
        unsigned int output_size = 0;
        const bool finalized = updated && digest_final(context, output.data(), &output_size) == 1;
        md_ctx_free(context);
        return finalized && output_size == Size;
    }
};

CryptoProvider::CryptoProvider(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

CryptoProvider::~CryptoProvider() = default;

std::unique_ptr<CryptoProvider> CryptoProvider::load_default() noexcept
{
    auto impl = std::make_unique<Impl>();
    impl->handle = process_crypto_library();
    if(impl->handle == nullptr) return nullptr;
    impl->md_ctx_new = load_function<MdCtxNew>(impl->handle, "EVP_MD_CTX_new");
    impl->md_ctx_free = load_function<MdCtxFree>(impl->handle, "EVP_MD_CTX_free");
    impl->sha256 = load_function<DigestType>(impl->handle, "EVP_sha256");
    impl->sha512 = load_function<DigestType>(impl->handle, "EVP_sha512");
    impl->digest_init = load_function<DigestInit>(impl->handle, "EVP_DigestInit_ex");
    impl->digest_update = load_function<DigestUpdate>(impl->handle, "EVP_DigestUpdate");
    impl->digest_final = load_function<DigestFinal>(impl->handle, "EVP_DigestFinal_ex");
    impl->raw_public_key =
        load_function<RawPublicKey>(impl->handle, "EVP_PKEY_new_raw_public_key");
    impl->pkey_free = load_function<PkeyFree>(impl->handle, "EVP_PKEY_free");
    impl->verify_init = load_function<VerifyInit>(impl->handle, "EVP_DigestVerifyInit");
    impl->verify = load_function<Verify>(impl->handle, "EVP_DigestVerify");
    if(!impl->complete()) return nullptr;
    return std::unique_ptr<CryptoProvider>(new CryptoProvider(std::move(impl)));
}

bool CryptoProvider::sha256(
    const void* data, std::size_t size, Sha256Digest& output) const noexcept
{
    return impl_ != nullptr && impl_->digest(impl_->sha256, data, size, output);
}

bool CryptoProvider::sha512(
    const void* data, std::size_t size, Sha512Digest& output) const noexcept
{
    return impl_ != nullptr && impl_->digest(impl_->sha512, data, size, output);
}

bool CryptoProvider::verify_ed25519(
    const Ed25519PublicKey& key, const void* message, std::size_t message_size,
    const Ed25519Signature& signature) const noexcept
{
    if(impl_ == nullptr || (message == nullptr && message_size != 0)) return false;
    auto* public_key =
        impl_->raw_public_key(kNidEd25519, nullptr, key.data(), key.size());
    if(public_key == nullptr) return false;
    auto* context = impl_->md_ctx_new();
    if(context == nullptr) {
        impl_->pkey_free(public_key);
        return false;
    }
    const bool initialized =
        impl_->verify_init(context, nullptr, nullptr, nullptr, public_key) == 1;
    const auto* input = message == nullptr
                            ? &kEmptyMessage
                            : static_cast<const unsigned char*>(message);
    const bool verified = initialized &&
                          impl_->verify(context, signature.data(), signature.size(),
                                        input, message_size) == 1;
    impl_->md_ctx_free(context);
    impl_->pkey_free(public_key);
    return verified;
}

}  // namespace lvgl_platform
