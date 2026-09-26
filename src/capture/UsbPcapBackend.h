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
};

class UsbPcapBackend {
public:
    // Enumeration only: never opens a capture handle or changes driver state.
    // An empty list means that no USBPcap control devices are available.
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
        const std::function<void()>& onStarted);
};

} // namespace apdu
