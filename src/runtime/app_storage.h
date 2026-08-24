#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dictpen {

class AppStorage {
public:
    AppStorage() noexcept;
    ~AppStorage();
    AppStorage(const AppStorage&) = delete;
    AppStorage& operator=(const AppStorage&) = delete;

    bool available() const noexcept { return directory_fd_ >= 0; }
    bool read(std::string_view record, std::vector<std::uint8_t>& output,
              std::size_t maximum_size) const noexcept;
    bool write_atomic(std::string_view record, const void* data, std::size_t size,
                      std::size_t maximum_size) const noexcept;

private:
    static bool valid_record_name(std::string_view value) noexcept;

    int directory_fd_ {-1};
};

}  // namespace dictpen
