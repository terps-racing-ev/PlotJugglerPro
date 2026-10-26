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
local build, not a relocatable `.app` bundle or DMG. See
`PLOTJUGGLER_PRO_BUILD.md` for details.

### MF4/MDF files on macOS

The MF4 loader needs Python 3 with the `asammdf` package. Create a virtual
environment and install the package:

```sh
python3 -m venv "$HOME/.venvs/plotjuggler-mf4"
"$HOME/.venvs/plotjuggler-mf4/bin/python" -m pip install --upgrade pip asammdf
```

Set the Python executable when launching PlotJuggler:

```sh
export PJ_ASAMMDF_PYTHON="$HOME/.venvs/plotjuggler-mf4/bin/python"
./install/macos-arm64/bin/plotjuggler
```

## What Not To Use

Do not use old upstream commands that clone `facontidavide/PlotJuggler`, build in
`build/PlotJuggler`, or package Snap/AppImage/Docker artifacts. They do not
describe this repository's current build.
