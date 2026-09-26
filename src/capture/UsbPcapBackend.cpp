#include "UsbPcapBackend.h"

#include <stdexcept>

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace apdu {
namespace {

// Wire ABI verified against the upstream BSD-2-Clause public header:
// https://github.com/desowin/usbpcap/blob/master/USBPcapDriver/include/USBPcap.h
// The setup order and exclusive, overlapped open follow USBPcapCMD/thread.c:
// https://github.com/desowin/usbpcap/blob/master/USBPcapCMD/thread.c
// These are independently implemented client operations; no driver or
// USBPcapCMD implementation is incorporated or required at build time.
constexpr DWORD setupBuffer =
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr DWORD startFiltering = CTL_CODE(
    FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS);
constexpr DWORD stopFiltering = CTL_CODE(
    FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS);
constexpr DWORD setSnaplen =
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr std::uint32_t snapshotBytes = 1024 * 1024;
constexpr std::uint32_t driverBufferBytes = 4 * 1024 * 1024;
constexpr DWORD readBufferBytes = 1024 * 1024;

[[noreturn]] void fail(const std::string& operation, DWORD error)
{
    std::string message = operation + " (Windows error " + std::to_string(error) + "). ";
    switch (error) {
    case ERROR_ACCESS_DENIED:
        message += "Run APDU Tracer as administrator and close other USBPcap or "
                   "Wireshark captures using this USB controller.";
        break;
    case ERROR_SHARING_VIOLATION:
    case ERROR_BUSY:
        message += "Close the other capture using this USB controller and try again.";
        break;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_NO_SUCH_DEVICE:
        message += "Refresh the controller list. Ensure the USBPcap driver is installed "
                   "and restart Windows if its installer requested it.";
        break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case ERROR_NO_SYSTEM_RESOURCES:
        message += "Free some system memory and try again.";
        break;
    default:
        message += "Refresh the controller list and try again. Check the USBPcap "
                   "driver installation if the problem persists.";
        break;
    }
    throw std::runtime_error(message);
}

class Handle {
public:
    explicit Handle(HANDLE value) noexcept : value_(value) {}
    ~Handle()
    {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const noexcept { return value_; }

private:
    HANDLE value_;
};

HANDLE createEvent()
{
    const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) {
        fail("Could not create the USBPcap I/O event", GetLastError());
    }
    return event;
}

class Operation {
public:
    explicit Operation(HANDLE device) : device_(device), event_(createEvent())
    {
        overlapped_.hEvent = event_.get();
    }

    ~Operation() { cancelAndDrain(); }
    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;

    OVERLAPPED* begin()
    {
        if (!ResetEvent(event_.get())) {
            fail("Could not reset the USBPcap I/O event", GetLastError());
        }
        overlapped_ = {};
        overlapped_.hEvent = event_.get();
        return &overlapped_;
    }

    // A null stopEvent is used only while stopping the filter during cleanup.
    bool finish(BOOL completed, DWORD error, HANDLE stopEvent,
                DWORD& bytes, const std::string& description)
    {
        if (completed) {
            return true;
        }
        if (error != ERROR_IO_PENDING) {
            fail(description, error);
        }
        pending_ = true;
        const HANDLE events[] = {stopEvent, event_.get()};
        const auto result = stopEvent
            ? WaitForMultipleObjects(2, events, FALSE, INFINITE)
            : WaitForSingleObject(event_.get(), 1000);
        if (stopEvent && result == WAIT_OBJECT_0) {
            cancelAndDrain();
            return false;
        }
        if (result == WAIT_FAILED) {
            fail("Could not wait for USBPcap I/O", GetLastError());
        }
        if (result == WAIT_TIMEOUT) {
            cancelAndDrain();
            return false;
        }
        const auto expected = stopEvent ? WAIT_OBJECT_0 + 1 : WAIT_OBJECT_0;
        if (result != expected) {
            throw std::runtime_error("USBPcap returned an unexpected I/O wait result.");
        }
        const auto succeeded = GetOverlappedResult(device_, &overlapped_, &bytes, FALSE);
        const auto completionError = succeeded ? ERROR_SUCCESS : GetLastError();
        // An incomplete result must keep its buffers alive until cancellation.
        pending_ = completionError == ERROR_IO_INCOMPLETE;
        if (!succeeded) {
            fail(description, completionError);
        }
        return true;
    }

private:
    void cancelAndDrain() noexcept
    {
        if (pending_) {
            CancelIoEx(device_, &overlapped_);
            DWORD ignored = 0;
            // Cancellation is asynchronous. The OVERLAPPED and read buffer
            // must remain alive until completion, even after CancelIoEx.
            // USBPcap uses a cancel-safe queue for its pending reads.
            // https://github.com/desowin/usbpcap/blob/master/USBPcapDriver/USBPcapBuffer.c
            GetOverlappedResult(device_, &overlapped_, &ignored, TRUE);
            pending_ = false;
        }
    }

    HANDLE device_;
    Handle event_;
    OVERLAPPED overlapped_{};
    bool pending_ = false;
};

bool control(HANDLE device, DWORD code, void* input, DWORD inputBytes,
             HANDLE stopEvent, const std::string& description)
{
    if (stopEvent && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
        return false;
    }
    Operation operation(device);
    DWORD bytes = 0;
    const auto completed = DeviceIoControl(device, code, input, inputBytes,
        nullptr, 0, &bytes, operation.begin());
    const auto error = completed ? ERROR_SUCCESS : GetLastError();
    return operation.finish(completed, error, stopEvent, bytes, description);
}

class FilterGuard {
public:
    explicit FilterGuard(HANDLE device) noexcept : device_(device) {}
    ~FilterGuard()
    {
        if (armed_) {
            try {
                control(device_, stopFiltering, nullptr, 0, nullptr,
                        "Could not stop USBPcap filtering");
            } catch (...) {
                // Closing the capture handle also stops filtering and frees
                // its kernel buffer (USBPcap IRP_MJ_CLEANUP implementation).
                // https://github.com/desowin/usbpcap/blob/master/USBPcapDriver/USBPcapGenReq.c
            }
        }
    }
    void arm() noexcept { armed_ = true; }

private:
    HANDLE device_;
    bool armed_ = false;
};

bool isControlName(std::wstring_view name)
{
    constexpr std::wstring_view prefix = L"USBPcap";
    if (!name.starts_with(prefix)) {
        return false;
    }
    const auto suffix = name.substr(prefix.size());
    if (suffix.empty() || suffix.size() > 5 || suffix.front() == L'0') {
        return false;
    }
    unsigned number = 0;
    for (const auto character : suffix) {
        if (character < L'0' || character > L'9') {
            return false;
        }
        number = number * 10 + static_cast<unsigned>(character - L'0');
    }
    return number <= 65535;
}

} // namespace

std::vector<CaptureDevice> UsbPcapBackend::devices()
{
    // The driver publishes \\DosDevices\\USBPcapN links. QueryDosDevice is
    // the documented alternative to USBPcapCMD's NT object enumeration:
    // https://github.com/desowin/usbpcap/blob/master/USBPcapDriver/USBPcapRootHubControl.c
    // https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-querydosdevicew
    std::vector<wchar_t> names(16384);
    DWORD length = 0;
    while ((length = QueryDosDeviceW(nullptr, names.data(),
                                    static_cast<DWORD>(names.size()))) == 0) {
        const auto error = GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER || names.size() >= 1024 * 1024) {
            fail("Could not enumerate USBPcap controllers", error);
        }
        names.resize(names.size() * 2);
    }

    std::vector<CaptureDevice> devices;
    std::size_t offset = 0;
    while (offset < length && names[offset] != L'\0') {
        const auto begin = names.begin() + static_cast<std::ptrdiff_t>(offset);
        const auto end = std::find(begin, names.begin() + length, L'\0');
        const std::wstring_view name(&names[offset], static_cast<std::size_t>(end - begin));
        if (isControlName(name)) {
            // USBPcap control names contain ASCII only.
            std::string ascii(name.size(), '\0');
            std::transform(name.begin(), name.end(), ascii.begin(),
                           [](wchar_t character) { return static_cast<char>(character); });
            devices.push_back({"\\\\.\\" + ascii, ascii + " (USB root hub)"});
        }
        offset += name.size() + 1;
    }
    std::sort(devices.begin(), devices.end(), [](const auto& left, const auto& right) {
        return left.path.size() != right.path.size()
            ? left.path.size() < right.path.size() : left.path < right.path;
    });
    return devices;
}

void UsbPcapBackend::capture(
    const std::string& path, std::stop_token stop,
    const std::function<void(std::span<const std::uint8_t>)>& onBytes,
    const std::function<void()>& onStarted)
{
    if (stop.stop_requested()) {
        return;
    }
    constexpr std::string_view prefix = "\\\\.\\";
    const std::wstring widePath(path.begin(), path.end());
    if (!path.starts_with(prefix) || !isControlName(std::wstring_view(widePath).substr(4))) {
        throw std::runtime_error("Select a USBPcap controller from the refreshed controller list.");
    }
    if (!onBytes) {
        throw std::runtime_error("USBPcap capture requires a callback to receive its PCAP stream.");
    }

    Handle stopEvent(createEvent());
    std::stop_callback notifyStop(stop, [event = stopEvent.get()] { SetEvent(event); });
    Handle device(CreateFileW(widePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                              nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
    if (device.get() == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        fail("Could not open " + path, error);
    }
    FilterGuard filter(device.get());
    auto snaplen = snapshotBytes;
    if (!control(device.get(), setSnaplen, &snaplen, sizeof(snaplen), stopEvent.get(),
                 "Could not configure the USBPcap snapshot length")) {
        return;
    }
    auto bufferSize = driverBufferBytes;
    if (!control(device.get(), setupBuffer, &bufferSize, sizeof(bufferSize), stopEvent.get(),
                 "Could not allocate the USBPcap capture buffer")) {
        return;
    }
    // Packed USBPCAP_ADDRESS_FILTER: four 32-bit address words, then one
    // BOOLEAN. A byte array expresses the exact 17-byte ABI without padding.
    std::array<std::uint8_t, 17> addresses{};
    addresses.back() = 1; // filterAll: existing and newly attached devices.
    filter.arm();
    if (!control(device.get(), startFiltering, addresses.data(),
                 static_cast<DWORD>(addresses.size()), stopEvent.get(),
                 "Could not start USBPcap filtering") || stop.stop_requested()) {
        return;
    }
    if (onStarted) {
        onStarted();
    }

    std::vector<std::uint8_t> buffer(readBufferBytes);
    Operation read(device.get()); // Destroy before its buffer and device.
    while (!stop.stop_requested()) {
        DWORD bytes = 0;
        const auto completed = ReadFile(device.get(), buffer.data(), readBufferBytes,
                                         &bytes, read.begin());
        const auto error = completed ? ERROR_SUCCESS : GetLastError();
        if (!read.finish(completed, error, stopEvent.get(), bytes,
                         "Could not read the USBPcap capture stream") || stop.stop_requested()) {
            return;
        }
        if (bytes == 0) {
            throw std::runtime_error("USBPcap closed its capture stream. Refresh the controller "
                                     "list and start the capture again.");
        }
        onBytes(std::span<const std::uint8_t>(buffer.data(), bytes));
    }
}

} // namespace apdu

#else

namespace apdu {

std::vector<CaptureDevice> UsbPcapBackend::devices()
{
    return {};
}

void UsbPcapBackend::capture(
    const std::string&, std::stop_token stop,
    const std::function<void(std::span<const std::uint8_t>)>&,
    const std::function<void()>&)
{
    if (!stop.stop_requested()) {
        throw std::runtime_error("Live USBPcap capture is available only on Windows. "
                                 "Open a saved USBPcap PCAP file for offline replay.");
    }
}

} // namespace apdu

#endif
