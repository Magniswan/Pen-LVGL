#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dictpen {

// Bounded private-record service. Paths and directory descriptors are never
// exposed to applications; quotas are enforced by the privileged broker.
class AppStorage {
public:
    AppStorage() noexcept;
    ~AppStorage();
    AppStorage(const AppStorage&) = delete;
    AppStorage& operator=(const AppStorage&) = delete;

    bool available() const noexcept { return socket_fd_ >= 0; }
    bool read(std::string_view record, std::vector<std::uint8_t>& output,
              std::size_t maximum_size) const noexcept;
    bool write_atomic(std::string_view record, const void* data, std::size_t size,
                      std::size_t maximum_size) const noexcept;

private:
    int socket_fd_ {-1};
    mutable std::uint64_t next_request_id_ {1};
};

}  // namespace dictpen
