#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace apdu {

struct CaptureDevice {
    std::string path;
    std::string label;
    // 0 selects the whole root hub; otherwise the attached device's USB address.
    int address = 0;
};

struct CaptureOptions {
    // USBPcap accepts 4 KiB to 128 MiB of non-paged kernel memory and drops
    // packets silently once this buffer fills, so the default leaves headroom.
    static constexpr std::uint32_t minBufferBytes = 4 * 1024;
    static constexpr std::uint32_t maxBufferBytes = 128 * 1024 * 1024;
    static constexpr std::uint32_t defaultBufferBytes = 16 * 1024 * 1024;

    // 1..127 asks the driver to capture only that device; 0 captures the
    // whole root hub, including devices attached while capturing.
    int address = 0;
    std::uint32_t bufferBytes = defaultBufferBytes;
};

class UsbPcapBackend {
public:
    // Enumeration only: never opens a capture handle or changes driver state.
    // Each root hub is listed first (address 0), followed by the devices
    // attached to it, including those behind external hubs. An empty list
    // means that no USBPcap control devices are available.
    static std::vector<CaptureDevice> devices();

    // Blocking; run on a worker thread. Both callbacks run on that thread.
    // onStarted runs only after the driver accepts the capture configuration.
    // onBytes receives arbitrary chunks of a classic PCAP stream (including
    // its global header), and its span is valid only during the callback.
    // Callbacks should return quickly so the driver's buffer can be drained.
    // A stop request cancels an idle read. Callback exceptions also clean up
    // the capture handle and are propagated to the caller.
    static void capture(
        const std::string& path,
        std::stop_token stop,
        const std::function<void(std::span<const std::uint8_t>)>& onBytes,
        const std::function<void()>& onStarted,
        const CaptureOptions& options = {});
};

} // namespace apdu
