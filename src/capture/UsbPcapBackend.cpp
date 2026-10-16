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
#include <cfgmgr32.h>
#include <setupapi.h>
#include <usbioctl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <map>
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
constexpr DWORD getHubSymlink =
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr DWORD setSnaplen =
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr std::uint32_t snapshotBytes = 1024 * 1024;
constexpr DWORD readBufferBytes = 1024 * 1024;

// Packed USBPCAP_ADDRESS_FILTER: four little-endian 32-bit address bitmaps
// (address N is bit N % 32 of word N / 32), then one BOOLEAN filterAll.
// A byte array expresses the exact 17-byte ABI without padding. Address bit
// 0 would add devices attached later; a single-device filter leaves it clear.
using AddressFilter = std::array<std::uint8_t, 17>;

AddressFilter addressFilter(int address)
{
    AddressFilter filter{};
    if (address == 0) {
        filter.back() = 1; // filterAll: existing and newly attached devices.
    } else {
        const auto bit = static_cast<unsigned>(address);
        filter[(bit / 32) * 4 + (bit % 32) / 8] =
            static_cast<std::uint8_t>(1u << (bit % 8));
    }
    return filter;
}

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

// --- Enumeration -----------------------------------------------------------
// Mirrors USBPcapCMD/enum.c: ask the control device for its root hub's
// interface link, then walk the hub's ports with the documented usbioctl.h
// IOCTLs, recursing into external hubs.
// https://github.com/desowin/usbpcap/blob/master/USBPcapCMD/enum.c
// https://learn.microsoft.com/windows-hardware/drivers/ddi/usbioctl/

std::string utf8(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring lowercase(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return text;
}

// Interface links arrive in kernel form (\??\...) or as a bare name.
std::wstring hubDevicePath(std::wstring_view link)
{
    if (link.starts_with(L"\\??\\")) {
        return L"\\\\.\\" + std::wstring(link.substr(4));
    }
    if (link.starts_with(L"\\")) {
        return std::wstring(link);
    }
    return L"\\\\.\\" + std::wstring(link);
}

std::wstring hubSymlink(const std::wstring& controlPath)
{
    // Zero desired access: USBPcap reserves the capture role for the one
    // handle opened with read access, so this neither blocks nor starts a
    // capture, and it works while another program is capturing.
    // https://github.com/desowin/usbpcap/blob/master/USBPcapDriver/USBPcapGenReq.c
    Handle control(CreateFileW(controlPath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr));
    if (control.get() == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        DWORD bytes = 0;
        if (DeviceIoControl(control.get(), getHubSymlink, nullptr, 0, buffer.data(),
                            static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), &bytes, nullptr)) {
            const auto count = std::min<std::size_t>(bytes / sizeof(wchar_t), buffer.size());
            return std::wstring(buffer.data(), wcsnlen(buffer.data(), count));
        }
        const auto error = GetLastError();
        if ((error != ERROR_INSUFFICIENT_BUFFER && error != ERROR_MORE_DATA) ||
            buffer.size() >= 32768) {
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}

// USB_NODE_CONNECTION_DRIVERKEY_NAME and USB_NODE_CONNECTION_NAME share one
// layout: port, total byte length, then the variable-length name.
constexpr std::size_t connectionNameOffset = 2 * sizeof(ULONG);
static_assert(offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME, DriverKeyName) == connectionNameOffset);
static_assert(offsetof(USB_NODE_CONNECTION_NAME, NodeName) == connectionNameOffset);

std::wstring connectionName(HANDLE hub, DWORD code, ULONG port)
{
    USB_NODE_CONNECTION_NAME probe{};
    probe.ConnectionIndex = port;
    DWORD bytes = 0;
    if (!DeviceIoControl(hub, code, &probe, sizeof(probe), &probe, sizeof(probe), &bytes, nullptr) ||
        probe.ActualLength <= connectionNameOffset || probe.ActualLength > 65536) {
        return {};
    }
    const auto length = probe.ActualLength;
    std::vector<ULONG> storage((length + sizeof(ULONG) - 1) / sizeof(ULONG));
    auto* name = reinterpret_cast<USB_NODE_CONNECTION_NAME*>(storage.data());
    name->ConnectionIndex = port;
    if (!DeviceIoControl(hub, code, name, length, name, length, &bytes, nullptr)) {
        return {};
    }
    const auto* text = reinterpret_cast<const wchar_t*>(
        reinterpret_cast<const std::uint8_t*>(storage.data()) + connectionNameOffset);
    return std::wstring(text, wcsnlen(text, (length - connectionNameOffset) / sizeof(wchar_t)));
}

class DeviceInfoList {
public:
    explicit DeviceInfoList(HDEVINFO value) noexcept : value_(value) {}
    ~DeviceInfoList()
    {
        if (value_ != INVALID_HANDLE_VALUE) {
            SetupDiDestroyDeviceInfoList(value_);
        }
    }
    DeviceInfoList(const DeviceInfoList&) = delete;
    DeviceInfoList& operator=(const DeviceInfoList&) = delete;
    HDEVINFO get() const noexcept { return value_; }

private:
    HDEVINFO value_;
};

std::wstring registryText(HDEVINFO list, SP_DEVINFO_DATA& device, DWORD property)
{
    std::array<wchar_t, 512> buffer{};
    if (!SetupDiGetDeviceRegistryPropertyW(list, &device, property, nullptr,
                                           reinterpret_cast<PBYTE>(buffer.data()),
                                           static_cast<DWORD>(sizeof(buffer) - sizeof(wchar_t)),
                                           nullptr)) {
        return {};
    }
    return buffer.data();
}

std::wstring nodeText(DEVINST node)
{
    for (const auto property : {CM_DRP_FRIENDLYNAME, CM_DRP_DEVICEDESC}) {
        std::array<wchar_t, 512> buffer{};
        ULONG size = static_cast<ULONG>(sizeof(buffer) - sizeof(wchar_t));
        if (CM_Get_DevNode_Registry_PropertyW(node, property, nullptr, buffer.data(), &size, 0) ==
                CR_SUCCESS && buffer[0] != L'\0') {
            return buffer.data();
        }
    }
    return {};
}

// Maps each present USB device's driver key (what the hub reports per port)
// to a readable name. Composite parents ("USB Composite Device") are named
// after their interface functions, such as the smart-card reader.
std::map<std::wstring, std::wstring> usbDeviceNames()
{
    std::map<std::wstring, std::wstring> names;
    DeviceInfoList list(SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                             DIGCF_ALLCLASSES | DIGCF_PRESENT));
    if (list.get() == INVALID_HANDLE_VALUE) {
        return names;
    }
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(list.get(), index, &device); ++index) {
        const auto key = registryText(list.get(), device, SPDRP_DRIVER);
        if (key.empty()) {
            continue;
        }
        auto name = registryText(list.get(), device, SPDRP_FRIENDLYNAME);
        if (name.empty()) {
            name = registryText(list.get(), device, SPDRP_DEVICEDESC);
        }
        if (lowercase(registryText(list.get(), device, SPDRP_SERVICE)) == L"usbccgp") {
            std::wstring functions;
            DEVINST child = 0;
            auto result = CM_Get_Child(&child, device.DevInst, 0);
            for (int count = 0; result == CR_SUCCESS && count < 4; ++count) {
                const auto function = nodeText(child);
                if (!function.empty() && functions.find(function) == std::wstring::npos) {
                    functions += (functions.empty() ? L"" : L", ") + function;
                }
                result = CM_Get_Sibling(&child, child, 0);
            }
            if (!functions.empty()) {
                name = functions;
            }
        }
        names.emplace(lowercase(key), name);
    }
    return names;
}

struct AttachedDevice {
    int address;
    std::wstring name;
};

void enumerateHub(const std::wstring& path, const std::map<std::wstring, std::wstring>& names,
                  std::vector<AttachedDevice>& found, int depth)
{
    // USB allows five tiers of external hubs below the root hub.
    if (depth > 6) {
        return;
    }
    Handle hub(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr));
    if (hub.get() == INVALID_HANDLE_VALUE) {
        return;
    }
    USB_NODE_INFORMATION node{};
    DWORD bytes = 0;
    if (!DeviceIoControl(hub.get(), IOCTL_USB_GET_NODE_INFORMATION, &node, sizeof(node),
                         &node, sizeof(node), &bytes, nullptr)) {
        return;
    }
    const ULONG ports = node.u.HubInformation.HubDescriptor.bNumberOfPorts;
    for (ULONG port = 1; port <= ports; ++port) {
        // The structure ends in a zero-length pipe array, so it lives in
        // zeroed storage. Sized without pipes, the reply carries only the
        // connection fields, as in USBPcapCMD.
        std::array<ULONG, (sizeof(USB_NODE_CONNECTION_INFORMATION) + sizeof(ULONG) - 1) /
                              sizeof(ULONG)> storage{};
        auto& connection = *reinterpret_cast<USB_NODE_CONNECTION_INFORMATION*>(storage.data());
        connection.ConnectionIndex = port;
        if (!DeviceIoControl(hub.get(), IOCTL_USB_GET_NODE_CONNECTION_INFORMATION,
                             &connection, sizeof(connection), &connection, sizeof(connection),
                             &bytes, nullptr) ||
            connection.ConnectionStatus != DeviceConnected) {
            continue;
        }
        if (connection.DeviceIsHub) {
            const auto external = connectionName(hub.get(), IOCTL_USB_GET_NODE_CONNECTION_NAME, port);
            if (!external.empty()) {
                enumerateHub(hubDevicePath(external), names, found, depth + 1);
            }
            continue;
        }
        if (connection.DeviceAddress < 1 || connection.DeviceAddress > 127) {
            continue;
        }
        std::wstring name;
        const auto key = connectionName(hub.get(), IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME, port);
        if (const auto match = names.find(lowercase(key)); !key.empty() && match != names.end()) {
            name = match->second;
        }
        if (name.empty()) {
            std::array<wchar_t, 32> fallback{};
            std::swprintf(fallback.data(), fallback.size(), L"USB device %04X:%04X",
                          connection.DeviceDescriptor.idVendor,
                          connection.DeviceDescriptor.idProduct);
            name = fallback.data();
        }
        found.push_back({connection.DeviceAddress, name});
    }
}

std::string deviceLabel(std::wstring_view control, std::wstring_view name, int address)
{
    // UTF-8 en dash keeps the controller visually separate from the name.
    const auto prefix = utf8(control) + " \xE2\x80\x93 ";
    return address == 0 ? prefix + "entire root hub"
                        : prefix + utf8(name) + " (address " + std::to_string(address) + ")";
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

    std::vector<std::wstring> controls;
    std::size_t offset = 0;
    while (offset < length && names[offset] != L'\0') {
        const auto begin = names.begin() + static_cast<std::ptrdiff_t>(offset);
        const auto end = std::find(begin, names.begin() + length, L'\0');
        const std::wstring_view name(&names[offset], static_cast<std::size_t>(end - begin));
        if (isControlName(name)) {
            controls.emplace_back(name);
        }
        offset += name.size() + 1;
    }
    std::sort(controls.begin(), controls.end(), [](const auto& left, const auto& right) {
        return left.size() != right.size() ? left.size() < right.size() : left < right;
    });

    // Device names are optional decoration: when SetupAPI or a hub query
    // fails, the root hub entry still captures the whole controller.
    const auto deviceNames = controls.empty() ? std::map<std::wstring, std::wstring>{} : usbDeviceNames();
    std::vector<CaptureDevice> devices;
    for (const auto& control : controls) {
        const auto path = L"\\\\.\\" + control;
        devices.push_back({utf8(path), deviceLabel(control, {}, 0), 0});
        std::vector<AttachedDevice> attached;
        if (const auto link = hubSymlink(path); !link.empty()) {
            enumerateHub(hubDevicePath(link), deviceNames, attached, 0);
        }
        std::sort(attached.begin(), attached.end(),
                  [](const auto& left, const auto& right) { return left.address < right.address; });
        for (const auto& device : attached) {
            devices.push_back({utf8(path), deviceLabel(control, device.name, device.address),
                               device.address});
        }
    }
    return devices;
}

void UsbPcapBackend::capture(
    const std::string& path, std::stop_token stop,
    const std::function<void(std::span<const std::uint8_t>)>& onBytes,
    const std::function<void()>& onStarted, const CaptureOptions& options)
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
    if (options.address < 0 || options.address > 127) {
        throw std::runtime_error("USB device addresses range from 1 to 127.");
    }
    if (options.bufferBytes < CaptureOptions::minBufferBytes ||
        options.bufferBytes > CaptureOptions::maxBufferBytes) {
        throw std::runtime_error("The USBPcap driver buffer must be between 4 KiB and 128 MiB.");
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
    auto bufferSize = options.bufferBytes;
    if (!control(device.get(), setupBuffer, &bufferSize, sizeof(bufferSize), stopEvent.get(),
                 "Could not allocate the USBPcap capture buffer")) {
        return;
    }
    auto addresses = addressFilter(options.address);
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
    const std::function<void()>&, const CaptureOptions&)
{
    if (!stop.stop_requested()) {
        throw std::runtime_error("Live USBPcap capture is available only on Windows. "
                                 "Open a saved USBPcap PCAP file for offline replay.");
    }
}

} // namespace apdu

#endif
