#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace apdu {

enum class StatusKind { Success, Warning, Error };

struct StatusInfo {
    std::uint16_t word{};
    StatusKind kind{StatusKind::Error};
    std::string text;
};

// Instruction name for a command APDU: ISO/IEC 7816-4 interindustry commands
// (also under the GSM class A0), and common GlobalPlatform and EMV commands
// under proprietary classes. Empty when the instruction is not recognised.
std::string commandName(std::span<const std::uint8_t> command);

// Meaning of the trailing SW1 SW2 of a response APDU (ISO/IEC 7816-4).
// No value for responses shorter than two bytes.
std::optional<StatusInfo> responseStatus(std::span<const std::uint8_t> response);

} // namespace apdu
