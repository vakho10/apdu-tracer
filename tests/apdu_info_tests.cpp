#include "core/ApduInfo.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Data = std::vector<std::uint8_t>;
using apdu::StatusKind;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void commandNames() {
    require(apdu::commandName(Data{0x00, 0xa4, 0x04, 0x00, 0x00}) == "SELECT", "SELECT");
    require(apdu::commandName(Data{0x00, 0xc0, 0x00, 0x00, 0x10}) == "GET RESPONSE", "GET RESPONSE");
    require(apdu::commandName(Data{0x0c, 0xb0, 0x00, 0x00, 0x10}) == "READ BINARY",
            "Secure messaging class bits must not hide the ISO instruction");
    require(apdu::commandName(Data{0x00, 0x20, 0x00, 0x81}) == "VERIFY", "Case 1 VERIFY");
    require(apdu::commandName(Data{0xa0, 0xa4, 0x00, 0x00, 0x02, 0x3f, 0x00}) == "SELECT",
            "GSM class A0 uses ISO instruction names");
    require(apdu::commandName(Data{0x80, 0x50, 0x00, 0x00, 0x08}) == "INITIALIZE UPDATE",
            "GlobalPlatform INITIALIZE UPDATE");
    require(apdu::commandName(Data{0x84, 0x82, 0x00, 0x00, 0x10}) == "EXTERNAL AUTHENTICATE",
            "GlobalPlatform EXTERNAL AUTHENTICATE with secure messaging");
    require(apdu::commandName(Data{0x80, 0xa8, 0x00, 0x00, 0x02, 0x83, 0x00}) == "GET PROCESSING OPTIONS",
            "EMV GET PROCESSING OPTIONS");
    require(apdu::commandName(Data{0x80, 0xa4, 0x04, 0x00}).empty(),
            "A proprietary class must not borrow ISO names");
    require(apdu::commandName(Data{0xff, 0xca, 0x00, 0x00, 0x00}).empty(), "Reader pseudo-APDU class FF");
    require(apdu::commandName(Data{0x00, 0x99, 0x00, 0x00}).empty(), "Unknown instruction");
    require(apdu::commandName(Data{0x00, 0xa4, 0x04}).empty(), "Too short for a command header");
}

void statusWords() {
    const auto status = [](Data response) { return apdu::responseStatus(response); };
    require(!status({0x90}), "One byte is not a status word");

    auto info = status({0x6f, 0x10, 0x84, 0x90, 0x00});
    require(info && info->word == 0x9000 && info->kind == StatusKind::Success && info->text == "Success",
            "9000 at the end of response data");
    info = status({0x61, 0x1c});
    require(info && info->kind == StatusKind::Success && info->text == "Success, 28 more bytes available",
            "61xx counts the available bytes");
    info = status({0x61, 0x00});
    require(info && info->text == "Success, 256 more bytes available", "6100 means 256 bytes");
    info = status({0x6a, 0x82});
    require(info && info->kind == StatusKind::Error && info->text == "File or application not found", "6A82");
    info = status({0x63, 0xc2});
    require(info && info->kind == StatusKind::Warning && info->text == "Verification failed, 2 tries left",
            "63Cx counts the remaining tries");
    info = status({0x6c, 0x1f});
    require(info && info->kind == StatusKind::Warning && info->text == "Wrong Le, retry with Le=1F", "6Cxx");
    info = status({0x69, 0x82});
    require(info && info->text == "Security status not satisfied", "6982");
    info = status({0x67, 0x00});
    require(info && info->text == "Wrong length", "6700 uses the SW1 group");
    info = status({0x91, 0xae});
    require(info && info->kind == StatusKind::Warning && info->text == "Application-specific status",
            "Other 9xxx statuses are application specific");
    info = status({0x12, 0x34});
    require(info && info->kind == StatusKind::Error && info->text == "Unknown status", "Invalid SW1");
}

} // namespace

int main() {
    try {
        commandNames();
        std::cout << "PASS command names\n";
        statusWords();
        std::cout << "PASS status words\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
