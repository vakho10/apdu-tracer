#pragma once
#include <cstdint>
#include <span>

namespace apdu {
// ISO 7816 short/extended cases 1-4; does not interpret instructions.
inline bool validCommandApdu(std::span<const std::uint8_t> data) {
    if (data.size() == 4 || data.size() == 5) return true;
    if (data.size() < 6) return false;
    if (data[4] != 0) {
        const auto end = std::size_t{5} + data[4];
        return data.size() == end || data.size() == end + 1;
    }
    if (data.size() < 7) return false;
    if (data.size() == 7) return true;
    const auto lc = (static_cast<unsigned>(data[5]) << 8) | data[6];
    const auto end = std::size_t{7} + lc;
    return lc != 0 && (data.size() == end || data.size() == end + 2);
}
} // namespace apdu
