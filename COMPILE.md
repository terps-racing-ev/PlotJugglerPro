# PlotJugglerPro Build Matrix

653This branch maintains these local build paths:

- Windows 11
- Visual Studio 2022
- CMake preset `windows-vs2022-pro`
- vcpkg at `C:\vcpkg`
- Qt 5.15.2 at `C:\Qt\5.15.2\msvc2019_64`
- Native Apple Silicon macOS
- Homebrew Qt 5 at `/opt/homebrew/opt/qt@5`
- CMake preset `macos-arm64-pro`

Linux, Snap, AppImage, Nix, Docker, ROS package builds, and Conan workflows are
upstream inheritance and are not currently maintained for PlotJugglerPro.

## Windows

Configure:

```powershell
cmake --preset windows-vs2022-pro
```

Build a runnable local install:

```powershell
cmake --build build\PlotJugglerPro --config Release --target install
```

Run:

```powershell
.\install\bin\plotjuggler.exe
```

Fast compile check:

```powershell
cmake --build build\PlotJugglerPro --config Release --target plotjuggler
```

Installer packaging:

```powershell
.\tools\package_windows_release.ps1
```

See `PLOTJUGGLER_PRO_BUILD.md` and `PLOTJUGGLER_PRO_WINDOWS_RELEASE.md` for the
current local workflow.

## macOS

Install the build tools and Qt:

```sh
brew install cmake qt@5
```

Build and install the native arm64 executable and plugins:

```sh
cmake --preset macos-arm64-pro
cmake --build --preset macos-arm64-pro-release-install
./install/macos-arm64/bin/plotjuggler
```

The preset explicitly targets arm64, uses the Homebrew Qt 5 installation, and
adds the Command Line Tools SDK libc++ headers explicitly. This produces a
local build. To create a relocatable `.app` bundle and DMG with a free ad-hoc
signature, run:

```sh
./tools/package_macos_release.sh
```

The script builds the Release app and plugins, embeds Qt and its platform
plugins, and writes the DMG under `dist/macos-arm64`. It targets macOS 13 and
Apple Silicon. Its ad-hoc signature is free and does not require an Apple
Developer account, but it is not notarized or signed with a developer identity.
The app bundle uses the existing PlotJuggler SVG artwork for its Finder and Dock
icon.
Gatekeeper may block the first launch: use Finder's right-click **Open**
option, or remove the quarantine attribute from the app after verifying its
source:

```sh
xattr -dr com.apple.quarantine /Applications/PlotJugglerPro.app
```

The release DMG includes a relocatable Python runtime and `asammdf` for MF4
loading. The packaging machine needs `uv` and network access to build it:

```sh
brew install uv
```

`PJ_MF4_PYTHON_VERSION` and `PJ_ASAMMDF_VERSION` can override the Python minor
version and pinned `asammdf` version used when packaging.

### MF4/MDF files during macOS development

Local builds use `python3` by default. Install `asammdf` in that Python
environment, or set `PJ_ASAMMDF_PYTHON` to another Python executable:

```sh
python3 -m venv "$HOME/.venvs/plotjuggler-mf4"
"$HOME/.venvs/plotjuggler-mf4/bin/python" -m pip install --upgrade pip asammdf
export PJ_ASAMMDF_PYTHON="$HOME/.venvs/plotjuggler-mf4/bin/python"
./install/macos-arm64/bin/plotjuggler
```

Packaged apps use their bundled Python automatically; the environment variable
can still override it for troubleshooting.

## What Not To Use

Do not use old upstream commands that clone `facontidavide/PlotJuggler`, build in
`build/PlotJuggler`, or package Snap/AppImage/Docker artifacts. They do not
describe this repository's current build.
