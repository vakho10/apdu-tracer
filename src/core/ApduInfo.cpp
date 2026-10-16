#include "ApduInfo.h"

#include <array>
#include <utility>

namespace apdu {
namespace {

struct Name {
    std::uint8_t code;
    const char* name;
};

// ISO/IEC 7816-4 interindustry instructions.
constexpr std::array interindustry{
    Name{0x04, "DEACTIVATE FILE"},        Name{0x0c, "ERASE RECORD"},
    Name{0x0e, "ERASE BINARY"},           Name{0x0f, "ERASE BINARY"},
    Name{0x20, "VERIFY"},                 Name{0x21, "VERIFY"},
    Name{0x22, "MANAGE SECURITY ENVIRONMENT"},
    Name{0x24, "CHANGE REFERENCE DATA"},  Name{0x26, "DISABLE VERIFICATION REQUIREMENT"},
    Name{0x28, "ENABLE VERIFICATION REQUIREMENT"},
    Name{0x2a, "PERFORM SECURITY OPERATION"},
    Name{0x2c, "RESET RETRY COUNTER"},    Name{0x44, "ACTIVATE FILE"},
    Name{0x46, "GENERATE ASYMMETRIC KEY PAIR"},
    Name{0x70, "MANAGE CHANNEL"},         Name{0x82, "EXTERNAL AUTHENTICATE"},
    Name{0x84, "GET CHALLENGE"},          Name{0x86, "GENERAL AUTHENTICATE"},
    Name{0x87, "GENERAL AUTHENTICATE"},   Name{0x88, "INTERNAL AUTHENTICATE"},
    Name{0xa0, "SEARCH BINARY"},          Name{0xa1, "SEARCH BINARY"},
    Name{0xa2, "SEARCH RECORD"},          Name{0xa4, "SELECT"},
    Name{0xb0, "READ BINARY"},            Name{0xb1, "READ BINARY"},
    Name{0xb2, "READ RECORD"},            Name{0xb3, "READ RECORD"},
    Name{0xc0, "GET RESPONSE"},           Name{0xc2, "ENVELOPE"},
    Name{0xc3, "ENVELOPE"},               Name{0xca, "GET DATA"},
    Name{0xcb, "GET DATA"},               Name{0xd0, "WRITE BINARY"},
    Name{0xd1, "WRITE BINARY"},           Name{0xd2, "WRITE RECORD"},
    Name{0xd6, "UPDATE BINARY"},          Name{0xd7, "UPDATE BINARY"},
    Name{0xda, "PUT DATA"},               Name{0xdb, "PUT DATA"},
    Name{0xdc, "UPDATE RECORD"},          Name{0xdd, "UPDATE RECORD"},
    Name{0xe0, "CREATE FILE"},            Name{0xe2, "APPEND RECORD"},
    Name{0xe4, "DELETE FILE"},            Name{0xe6, "TERMINATE DF"},
    Name{0xe8, "TERMINATE EF"},           Name{0xfe, "TERMINATE CARD USAGE"},
};

// GlobalPlatform card management and EMV payment instructions, which use
// proprietary classes such as 80 and 84.
constexpr std::array proprietary{
    Name{0x1e, "APPLICATION BLOCK"},      Name{0x50, "INITIALIZE UPDATE"},
    Name{0x82, "EXTERNAL AUTHENTICATE"},  Name{0xa8, "GET PROCESSING OPTIONS"},
    Name{0xae, "GENERATE AC"},            Name{0xca, "GET DATA"},
    Name{0xcb, "GET DATA"},               Name{0xd8, "PUT KEY"},
    Name{0xe2, "STORE DATA"},             Name{0xe4, "DELETE"},
    Name{0xe6, "INSTALL"},                Name{0xe8, "LOAD"},
    Name{0xf0, "SET STATUS"},             Name{0xf2, "GET STATUS"},
};

template <std::size_t N>
const char* find(const std::array<Name, N>& names, std::uint8_t ins) {
    for (const auto& entry : names)
        if (entry.code == ins) return entry.name;
    return nullptr;
}

std::string hexByte(unsigned value) {
    constexpr char digits[] = "0123456789ABCDEF";
    return {digits[(value >> 4) & 0xf], digits[value & 0xf]};
}

} // namespace

std::string commandName(std::span<const std::uint8_t> command) {
    if (command.size() < 4) return {};
    const auto cla = command[0];
    const auto ins = command[1];
    if (cla == 0xff) return {}; // Reserved for PPS and reader pseudo-APDUs.
    // b8 clear: interindustry class. A0 is the GSM SIM class with ISO coding.
    const bool iso = (cla & 0x80) == 0 || cla == 0xa0;
    const char* name = iso ? find(interindustry, ins) : find(proprietary, ins);
    return name ? name : std::string{};
}

std::optional<StatusInfo> responseStatus(std::span<const std::uint8_t> response) {
    if (response.size() < 2) return std::nullopt;
    const unsigned sw1 = response[response.size() - 2];
    const unsigned sw2 = response[response.size() - 1];
    StatusInfo info;
    info.word = static_cast<std::uint16_t>((sw1 << 8) | sw2);
    const auto set = [&info](StatusKind kind, std::string text) {
        info.kind = kind;
        info.text = std::move(text);
    };
    const auto count = [sw2] { return std::to_string(sw2 == 0 ? 256 : sw2); };

    switch (info.word) {
    case 0x9000: set(StatusKind::Success, "Success"); return info;
    case 0x6281: set(StatusKind::Warning, "Returned data may be corrupted"); return info;
    case 0x6282: set(StatusKind::Warning, "End of file or record reached"); return info;
    case 0x6283: set(StatusKind::Warning, "Selected file deactivated"); return info;
    case 0x6300: set(StatusKind::Warning, "Verification failed"); return info;
    case 0x6581: set(StatusKind::Error, "Memory failure"); return info;
    case 0x6881: set(StatusKind::Error, "Logical channel not supported"); return info;
    case 0x6882: set(StatusKind::Error, "Secure messaging not supported"); return info;
    case 0x6883: set(StatusKind::Error, "Last command of the chain expected"); return info;
    case 0x6884: set(StatusKind::Error, "Command chaining not supported"); return info;
    case 0x6981: set(StatusKind::Error, "Command incompatible with file structure"); return info;
    case 0x6982: set(StatusKind::Error, "Security status not satisfied"); return info;
    case 0x6983: set(StatusKind::Error, "Authentication method blocked"); return info;
    case 0x6984: set(StatusKind::Error, "Reference data not usable"); return info;
    case 0x6985: set(StatusKind::Error, "Conditions of use not satisfied"); return info;
    case 0x6986: set(StatusKind::Error, "Command not allowed (no current EF)"); return info;
    case 0x6987: set(StatusKind::Error, "Expected secure messaging data objects missing"); return info;
    case 0x6988: set(StatusKind::Error, "Incorrect secure messaging data objects"); return info;
    case 0x6a80: set(StatusKind::Error, "Incorrect parameters in the data field"); return info;
    case 0x6a81: set(StatusKind::Error, "Function not supported"); return info;
    case 0x6a82: set(StatusKind::Error, "File or application not found"); return info;
    case 0x6a83: set(StatusKind::Error, "Record not found"); return info;
    case 0x6a84: set(StatusKind::Error, "Not enough memory space in the file"); return info;
    case 0x6a85: set(StatusKind::Error, "Nc inconsistent with TLV structure"); return info;
    case 0x6a86: set(StatusKind::Error, "Incorrect parameters P1-P2"); return info;
    case 0x6a87: set(StatusKind::Error, "Nc inconsistent with parameters P1-P2"); return info;
    case 0x6a88: set(StatusKind::Error, "Referenced data not found"); return info;
    case 0x6a89: set(StatusKind::Error, "File already exists"); return info;
    case 0x6a8a: set(StatusKind::Error, "DF name already exists"); return info;
    case 0x6b00: set(StatusKind::Error, "Wrong parameters P1-P2"); return info;
    case 0x6d00: set(StatusKind::Error, "Instruction not supported"); return info;
    case 0x6e00: set(StatusKind::Error, "Class not supported"); return info;
    case 0x6f00: set(StatusKind::Error, "No precise diagnosis"); return info;
    default: break;
    }

    switch (sw1) {
    case 0x61: set(StatusKind::Success, "Success, " + count() + " more bytes available"); break;
    case 0x62: set(StatusKind::Warning, "Warning, state unchanged"); break;
    case 0x63:
        if ((sw2 & 0xf0) == 0xc0)
            set(StatusKind::Warning, "Verification failed, " + std::to_string(sw2 & 0x0f) + " tries left");
        else
            set(StatusKind::Warning, "Warning, state changed");
        break;
    case 0x64: set(StatusKind::Error, "Execution error, state unchanged"); break;
    case 0x65: set(StatusKind::Error, "Execution error, state changed"); break;
    case 0x66: set(StatusKind::Error, "Security-related error"); break;
    case 0x67: set(StatusKind::Error, "Wrong length"); break;
    case 0x68: set(StatusKind::Error, "Function in CLA not supported"); break;
    case 0x69: set(StatusKind::Error, "Command not allowed"); break;
    case 0x6a: set(StatusKind::Error, "Wrong parameters"); break;
    case 0x6c: set(StatusKind::Warning, "Wrong Le, retry with Le=" + hexByte(sw2)); break;
    default:
        if ((sw1 & 0xf0) == 0x90) set(StatusKind::Warning, "Application-specific status");
        else set(StatusKind::Error, "Unknown status");
        break;
    }
    return info;
}

} // namespace apdu
