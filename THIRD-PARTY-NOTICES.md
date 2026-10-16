# Third-party notices

APDU Tracer itself is licensed under the [MIT License](LICENSE). Release
packages also contain the following third-party components.

## Qt

The application uses the [Qt framework](https://www.qt.io/) (Qt Core, Gui,
Widgets and Network, with their platform and image format plugins). Release
builds use Qt 6.8.1.

Qt is used under the GNU Lesser General Public License version 3. The
license texts are included as [`licenses/LGPL-3.0.txt`](licenses/LGPL-3.0.txt)
and [`licenses/GPL-3.0.txt`](licenses/GPL-3.0.txt), which the LGPL builds on.

Qt is linked dynamically: its unmodified libraries are shipped as separate
DLLs in `bin/` and plugins in `plugins/`, and they can be replaced with other
compatible Qt 6 builds. The Qt source code is available from
<https://download.qt.io/> and <https://code.qt.io/>.

Qt is a registered trademark of The Qt Company Ltd. and its subsidiaries.

## Microsoft Direct3D shader compiler

`bin/D3Dcompiler_47.dll` is deployed with Qt and is redistributed under the
terms Microsoft provides for this component with the Windows SDK.
