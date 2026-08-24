#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace lvgl_platform {

using Sha256Digest = std::array<std::uint8_t, 32>;
using Sha512Digest = std::array<std::uint8_t, 64>;
using Ed25519PublicKey = std::array<std::uint8_t, 32>;
using Ed25519Signature = std::array<std::uint8_t, 64>;

class CryptoProvider {
public:
    ~CryptoProvider();
    CryptoProvider(const CryptoProvider&) = delete;
    CryptoProvider& operator=(const CryptoProvider&) = delete;

    static std::unique_ptr<CryptoProvider> load_default() noexcept;

    bool sha256(const void* data, std::size_t size, Sha256Digest& output) const noexcept;
    bool sha512(const void* data, std::size_t size, Sha512Digest& output) const noexcept;
    bool verify_ed25519(
        const Ed25519PublicKey& key, const void* message, std::size_t message_size,
        const Ed25519Signature& signature) const noexcept;

private:
    struct Impl;
    explicit CryptoProvider(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lvgl_platform
