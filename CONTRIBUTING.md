# Contributing to APDU Tracer

APDU Tracer is a C++20 and Qt 6 Widgets application built with CMake. Windows is
the primary platform; live capture uses the separately installed USBPcap driver.

## Getting started

Clone the repository and install Visual Studio 2022 with the C++ desktop tools,
a Windows SDK, CMake 3.21 or newer, and Qt 6.5 or newer with an MSVC x64 kit.
The current build has been tested with Qt 6.8.1 and MSVC 2022. See the
[README](README.md#build-on-windows) for setup and deployment details.

From PowerShell in the repository root, adjust the Qt path for your installation:

```powershell
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.1/msvc2022_64"
cmake --build build/windows --config Release --parallel
$env:PATH = "C:\Qt\6.8.1\msvc2022_64\bin;" + $env:PATH
ctest --test-dir build/windows -C Release --output-on-failure
& .\build\windows\Release\APDUTracer.exe
```

USBPcap and reader hardware are needed for live capture only. Automated tests
use synthetic input and substituted driver calls.

## Project layout

- `src/core/`: standard C++ PCAP, USBPcap, and CCID decoding.
- `src/capture/`: Windows driver access and the portable fallback.
- `src/ui/`: Qt capture worker, bounded queue, and main window.
- `tests/`: protocol regression tests, fixture generation, and the Windows
  backend harness.
- `ui-tests/`: offscreen Qt replay and worker tests.
- `CMakeLists.txt`: build targets, CTest registration, and runtime deployment.

## Validation

Run the build and CTest commands above before opening a pull request. The full
Windows build has five CTest checks:

| Check | Coverage |
| --- | --- |
| `decoder_tests` | PCAP parsing, USB direction, CCID framing, APDU validation, and malformed input |
| `usbpcap_backend_tests` | Windows capture setup, streaming, cancellation, and error cleanup using substituted Win32 calls |
| `generate_fixture` | Creates the synthetic capture required by the Qt replay test |
| `qt_smoke_tests` | Replays a capture through the worker and verifies command and response text in the offscreen window |
| `worker_tests` | Replay under backpressure, cancellation, and bounded display memory |

The Windows backend check is registered only on Windows. The fixture and Qt
checks require the default `APDU_BUILD_GUI=ON`; disabling the GUI allows work on
the core without Qt:

```powershell
cmake -S . -B build/core -DAPDU_BUILD_GUI=OFF
cmake --build build/core --config Release
ctest --test-dir build/core -C Release --output-on-failure
```

CI uses CMake and CTest; see [the workflow](.github/workflows/ci.yml) for its
current configuration. Keep tests focused on observable behavior, especially
decoding boundaries, capture cancellation, and resource cleanup. Add a
regression case when fixing a bug in these areas.

Live hardware compatibility has not yet been validated. For capture changes,
perform the [manual hardware check](README.md#verification) when a reader is
available and report the reader, Windows version, and result in the PR. State
clearly when validation used synthetic captures only.

## Code and pull requests

Follow the existing C++ formatting and naming conventions. Keep decoder code
independent of Qt and Windows, and keep driver-specific code in `src/capture/`.
Address compiler warnings introduced by your changes.

Keep each PR focused. Describe the problem, the resulting behavior, and the
validation performed. Update the README when supported capture formats, usage,
or limitations change. Use concise, descriptive commit messages.

The CMake install target deploys the application and Qt runtime into a local
folder on Windows. A release installer and automated publishing workflow are
not currently provided.

## License

By contributing, you agree that your contributions are licensed under the
project's [MIT License](LICENSE).
