// Compile this file as a standalone Windows test executable. It includes the
// backend with only its driver-facing Win32 calls replaced. Real Win32 events
// and waits exercise cancellation without opening or configuring any device.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fake {

constexpr DWORD snaplenCode = 0x00226010;
constexpr DWORD bufferCode = 0x00226000;
constexpr DWORD startCode = 0x0022e004;
constexpr DWORD stopCode = 0x0022e008;
constexpr std::array<unsigned char, 4> streamBytes{0xd4, 0xc3, 0xb2, 0xa1};

struct State {
    unsigned opens = 0;
    unsigned closes = 0;
    unsigned reads = 0;
    unsigned cancels = 0;
    unsigned drains = 0;
    DWORD failControl = 0;
    bool zeroRead = false;
    bool readError = false;
    bool completeRead = false;
    OVERLAPPED* pending = nullptr;
    bool cancelled = false;
    std::vector<DWORD> controls;
    std::vector<unsigned char> filter;
    std::uint32_t bufferBytes = 0;
    std::promise<void> readPending;
};

State state;
int handleTag;

HANDLE deviceHandle() { return &handleTag; }

void require(bool condition, const char* description)
{
    if (!condition) {
        throw std::runtime_error(description);
    }
}

HANDLE open(LPCWSTR path, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES,
            DWORD creation, DWORD flags, HANDLE)
{
    require(std::wstring(path) == L"\\\\.\\USBPcap1", "Unexpected device path");
    require(access == (GENERIC_READ | GENERIC_WRITE) && sharing == 0,
            "Capture must request exclusive read/write access");
    require(creation == OPEN_EXISTING && flags == FILE_FLAG_OVERLAPPED,
            "Capture must open the existing device for overlapped I/O");
    ++state.opens;
    return deviceHandle();
}

BOOL close(HANDLE handle)
{
    if (handle == deviceHandle()) {
        ++state.closes;
        return TRUE;
    }
    return CloseHandle(handle);
}

BOOL control(HANDLE handle, DWORD code, LPVOID input, DWORD size,
             LPVOID output, DWORD outputSize, LPDWORD bytes, LPOVERLAPPED operation)
{
    require(handle == deviceHandle() && operation && operation->hEvent,
            "Every driver IOCTL must have a valid OVERLAPPED event");
    require(output == nullptr && outputSize == 0, "Unexpected IOCTL output buffer");
    state.controls.push_back(code);
    if (code == snaplenCode || code == bufferCode) {
        require(size == 4 && input, "Size IOCTL ABI must be exactly UINT32");
        std::uint32_t configured = 0;
        std::memcpy(&configured, input, sizeof(configured));
        require(configured >= 65536, "Buffer and snapshot must hold extended APDUs");
        if (code == bufferCode) {
            state.bufferBytes = configured;
        }
    } else if (code == startCode) {
        require(size == 17 && input, "Address filter must use its packed 17-byte ABI");
        const auto* filter = static_cast<const unsigned char*>(input);
        state.filter.assign(filter, filter + size);
    } else {
        require(code == stopCode && input == nullptr && size == 0,
                "Unexpected driver IOCTL");
    }
    if (code == state.failControl) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    *bytes = 0;
    return TRUE;
}

BOOL read(HANDLE handle, LPVOID buffer, DWORD size, LPDWORD bytes, LPOVERLAPPED operation)
{
    require(handle == deviceHandle(), "Read must use the capture handle");
    ++state.reads;
    *bytes = 0;
    if (state.readError) {
        SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        return FALSE;
    }
    if (state.zeroRead) {
        return TRUE;
    }
    if (state.reads == 1) {
        require(size >= streamBytes.size(), "Read buffer is too small");
        std::copy(streamBytes.begin(), streamBytes.end(), static_cast<unsigned char*>(buffer));
        *bytes = static_cast<DWORD>(streamBytes.size());
        return TRUE;
    }
    state.pending = operation;
    state.readPending.set_value();
    if (state.completeRead) {
        SetEvent(operation->hEvent);
    }
    SetLastError(ERROR_IO_PENDING);
    return FALSE;
}

BOOL cancel(HANDLE handle, LPOVERLAPPED operation)
{
    require(handle == deviceHandle() && operation == state.pending,
            "Cancellation must target the pending read");
    ++state.cancels;
    state.cancelled = true;
    SetEvent(operation->hEvent);
    return TRUE;
}

BOOL result(HANDLE handle, LPOVERLAPPED operation, LPDWORD bytes, BOOL wait)
{
    require(handle == deviceHandle() && operation == state.pending,
            "Completion must refer to the pending read");
    if (state.completeRead && !state.cancelled) {
        require(wait == FALSE, "A signalled read must already be complete");
        state.pending = nullptr;
        *bytes = static_cast<DWORD>(streamBytes.size());
        return TRUE;
    }
    require(state.cancelled,
            "The pending read must be cancelled before its storage is released");
    require(wait == TRUE, "Cancellation must be drained before freeing OVERLAPPED");
    ++state.drains;
    state.pending = nullptr;
    *bytes = 0;
    SetLastError(ERROR_OPERATION_ABORTED);
    return FALSE;
}

} // namespace fake

#define CreateFileW fake::open
#define CloseHandle fake::close
#define DeviceIoControl fake::control
#define ReadFile fake::read
#define CancelIoEx fake::cancel
#define GetOverlappedResult fake::result
#include "../src/capture/UsbPcapBackend.cpp"
#undef CreateFileW
#undef CloseHandle
#undef DeviceIoControl
#undef ReadFile
#undef CancelIoEx
#undef GetOverlappedResult

namespace {

constexpr auto devicePath = "\\\\.\\USBPcap1";

void expectFailure(const std::function<void()>& operation, const char* expected)
{
    bool failed = false;
    try {
        operation();
    } catch (const std::runtime_error& error) {
        failed = std::string(error.what()).find(expected) != std::string::npos;
    }
    fake::require(failed, "Expected error was not propagated");
}

// filterAll set and every address bit clear: the whole root hub.
std::vector<unsigned char> allDevicesFilter()
{
    std::vector<unsigned char> filter(17, 0);
    filter[16] = 1;
    return filter;
}

std::vector<unsigned char> singleAddressFilter(unsigned byteIndex, unsigned char bit)
{
    std::vector<unsigned char> filter(17, 0);
    filter[byteIndex] = bit;
    return filter;
}

void checkCleanup()
{
    fake::require(fake::state.opens == 1 && fake::state.closes == 1,
                  "Every capture handle must be closed exactly once");
    fake::require(fake::state.controls == std::vector<DWORD>{
                      fake::snaplenCode, fake::bufferCode, fake::startCode, fake::stopCode},
                  "Expected snaplen, buffer, start, stop IOCTL sequence");
}

void cancellationTest()
{
    fake::state = {};
    std::stop_source source;
    auto readPending = fake::state.readPending.get_future();
    bool reachedPendingRead = false;
    std::jthread stopper([&] {
        reachedPendingRead = readPending.wait_for(std::chrono::seconds(2)) ==
                             std::future_status::ready;
        source.request_stop();
    });
    bool started = false;
    std::vector<std::uint8_t> received;
    apdu::UsbPcapBackend::capture(devicePath, source.get_token(), [&](auto bytes) {
        fake::require(started, "Bytes arrived before the capture-start callback");
        received.insert(received.end(), bytes.begin(), bytes.end());
    }, [&] { started = true; });
    stopper.join();
    fake::require(reachedPendingRead, "Capture never reached an idle overlapped read");
    fake::require(std::equal(received.begin(), received.end(),
                            fake::streamBytes.begin(), fake::streamBytes.end()),
                  "Driver stream must be forwarded without modification");
    fake::require(fake::state.cancels == 1 && fake::state.drains == 1,
                  "Stop must cancel and drain exactly one pending read");
    fake::require(fake::state.filter == allDevicesFilter(),
                  "Address All must send filterAll with an empty address bitmap");
    fake::require(fake::state.bufferBytes == apdu::CaptureOptions::defaultBufferBytes,
                  "Capture must request the default driver buffer");
    checkCleanup();
}

void addressFilterTest()
{
    // USBPCAP_ADDRESS_FILTER.addresses[address / 32] bit (address % 32),
    // little-endian, with filterAll cleared so only that device is captured.
    struct Case {
        int address;
        unsigned byteIndex;
        unsigned char bit;
    };
    for (const auto [address, byteIndex, bit] : {Case{5, 0, 0x20}, Case{1, 0, 0x02},
                                                 Case{37, 4, 0x20}, Case{127, 15, 0x80}}) {
        fake::state = {};
        apdu::CaptureOptions options;
        options.address = address;
        options.bufferBytes = 32 * 1024 * 1024;
        unsigned chunks = 0;
        expectFailure([&] {
            apdu::UsbPcapBackend::capture(devicePath, {}, [&](auto) {
                ++chunks;
                throw std::runtime_error("filter checked");
            }, {}, options);
        }, "filter checked");
        fake::require(chunks == 1, "A filtered capture must still stream bytes");
        fake::require(fake::state.filter == singleAddressFilter(byteIndex, bit),
                      "A specific address must send only its bit with filterAll cleared");
        fake::require(fake::state.bufferBytes == options.bufferBytes,
                      "Capture must request the configured driver buffer");
        checkCleanup();
    }

    fake::state = {};
    apdu::CaptureOptions all;
    all.address = 0;
    expectFailure([&] {
        apdu::UsbPcapBackend::capture(devicePath, {}, [](auto) {
            throw std::runtime_error("filter checked");
        }, {}, all);
    }, "filter checked");
    fake::require(fake::state.filter == allDevicesFilter(),
                  "Address 0 (All) must send filterAll");
    checkCleanup();
}

void enumerationHelperTest()
{
    fake::require(apdu::hubDevicePath(L"\\??\\USB#ROOT_HUB30#4&1#{f18a0e88}") ==
                      L"\\\\.\\USB#ROOT_HUB30#4&1#{f18a0e88}",
                  "Kernel interface links must become Win32 device paths");
    fake::require(apdu::hubDevicePath(L"USB#VID_05E3&PID_0610#5&2#{f18a0e88}") ==
                      L"\\\\.\\USB#VID_05E3&PID_0610#5&2#{f18a0e88}",
                  "Bare external hub names must become Win32 device paths");
    fake::require(apdu::deviceLabel(L"USBPcap2", L"Smart card reader", 5) ==
                      "USBPcap2 \xE2\x80\x93 Smart card reader (address 5)",
                  "Attached devices must show their controller, name, and address");
    fake::require(apdu::deviceLabel(L"USBPcap1", {}, 0) == "USBPcap1 \xE2\x80\x93 entire root hub",
                  "Root hub entries must say they capture the whole hub");
    fake::require(apdu::utf8(L"Lecteur é") == "Lecteur \xC3\xA9",
                  "Device names must be converted to UTF-8");
}

void failureTests()
{
    fake::state = {};
    expectFailure([] {
        apdu::UsbPcapBackend::capture(devicePath, {}, [](auto) {}, [] {
            throw std::runtime_error("start callback failure");
        });
    }, "start callback failure");
    fake::require(fake::state.reads == 0, "A failed start callback must not read");
    checkCleanup();

    fake::state = {};
    expectFailure([] {
        apdu::UsbPcapBackend::capture(devicePath, {}, [](auto) {
            throw std::runtime_error("bytes callback failure");
        }, {});
    }, "bytes callback failure");
    checkCleanup();

    fake::state = {};
    fake::state.zeroRead = true;
    expectFailure([] { apdu::UsbPcapBackend::capture(devicePath, {}, [](auto) {}, {}); },
                  "closed its capture stream");
    checkCleanup();

    fake::state = {};
    fake::state.readError = true;
    expectFailure([] { apdu::UsbPcapBackend::capture(devicePath, {}, [](auto) {}, {}); },
                  "Refresh the controller list");
    checkCleanup();

    fake::state = {};
    fake::state.failControl = fake::startCode;
    bool started = false;
    expectFailure([&] {
        apdu::UsbPcapBackend::capture(devicePath, {}, [](auto) {}, [&] { started = true; });
    }, "administrator");
    fake::require(!started, "Failed driver startup must not announce success");
    checkCleanup();

    fake::state = {};
    fake::state.failControl = fake::stopCode;
    expectFailure([] {
        apdu::UsbPcapBackend::capture(devicePath, {}, [](auto) {
            throw std::runtime_error("original failure");
        }, {});
    }, "original failure");
    checkCleanup();
}

void completionTest()
{
    fake::state = {};
    fake::state.completeRead = true;
    std::stop_source source;
    unsigned chunks = 0;
    apdu::UsbPcapBackend::capture(devicePath, source.get_token(), [&](auto bytes) {
        fake::require(std::equal(bytes.begin(), bytes.end(),
                                fake::streamBytes.begin(), fake::streamBytes.end()),
                      "Pending completion must forward the same stream bytes");
        if (++chunks == 2) {
            source.request_stop();
        }
    }, {});
    fake::require(chunks == 2 && fake::state.cancels == 0,
                  "Completed reads must not be cancelled during cleanup");
    checkCleanup();
}

void preflightTest()
{
    fake::state = {};
    std::stop_source source;
    source.request_stop();
    apdu::UsbPcapBackend::capture(devicePath, source.get_token(), {}, {});
    expectFailure([] { apdu::UsbPcapBackend::capture("C:\\file", {}, [](auto) {}, {}); },
                  "Select a USBPcap controller");
    apdu::CaptureOptions badAddress;
    badAddress.address = 128;
    expectFailure([&] { apdu::UsbPcapBackend::capture(devicePath, {}, [](auto) {}, {}, badAddress); },
                  "addresses range");
    for (const std::uint32_t bytes : {apdu::CaptureOptions::minBufferBytes - 1,
                                      apdu::CaptureOptions::maxBufferBytes + 1}) {
        apdu::CaptureOptions badBuffer;
        badBuffer.bufferBytes = bytes;
        expectFailure([&] { apdu::UsbPcapBackend::capture(devicePath, {}, [](auto) {}, {}, badBuffer); },
                      "driver buffer");
    }
    fake::require(fake::state.opens == 0, "Preflight failures must not open any device");
}

} // namespace

int main()
{
    try {
        preflightTest();
        cancellationTest();
        completionTest();
        addressFilterTest();
        enumerationHelperTest();
        failureTests();
        std::cout << "USBPcap backend lifecycle tests passed (mock driver; real Win32 waits).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "USBPcap backend test failed: " << error.what() << '\n';
        return 1;
    }
}
