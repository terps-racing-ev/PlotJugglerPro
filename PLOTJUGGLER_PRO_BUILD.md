# PlotJugglerPro Local Build

This document describes the local Windows workflow and native Apple Silicon
macOS build. Other inherited upstream build paths are not maintained for this
branch.

## Requirements

### Windows

- Visual Studio 2022
- CMake
- vcpkg at `C:\vcpkg`
- Qt 5.15.2 at `C:\Qt\5.15.2\msvc2019_64`
- Qt Installer Framework only when creating an installer

### macOS Apple Silicon

- Apple Silicon Mac with Xcode Command Line Tools
- Homebrew
- CMake and Qt 5:

```sh
brew install cmake qt@5
```

The preset expects Homebrew at `/opt/homebrew` and Qt 5 at
`/opt/homebrew/opt/qt@5`. It also adds the Command Line Tools SDK's libc++
headers explicitly, which is needed on systems where the active compiler does
not find those headers automatically.

## Configure

### Windows

```powershell
cmake --preset windows-vs2022-pro
```

The preset writes to:

```powershell
build\PlotJugglerPro
```

## Build

Fast compile check:

```powershell
cmake --build build\PlotJugglerPro --config Release --target plotjuggler
```

Build the runnable local install:

```powershell
cmake --build build\PlotJugglerPro --config Release --target install
```

Equivalent preset:

```powershell
cmake --build --preset windows-vs2022-pro-release-install
```

## Launch

```powershell
.\install\bin\plotjuggler.exe
```

`QtWebEngineProcess.exe` must sit beside the installed app for WebEngine map
support, but it is not launched directly.

### macOS Apple Silicon

Configure and build/install the native arm64 executable and plugins:

```sh
cmake --preset macos-arm64-pro
cmake --build --preset macos-arm64-pro-release-install
```

Run the installed executable:

```sh
./install/macos-arm64/bin/plotjuggler
```

For a fast compile check without installing:

```sh
cmake --build --preset macos-arm64-pro-release --target plotjuggler
```

This is a local build that links against the Homebrew Qt installation. It does
not create a relocatable `.app` bundle or DMG.

## Troubleshooting

If `install\bin\plotjuggler.exe` is running, the install target can fail because
Windows will not overwrite the executable. Close PlotJuggler and rebuild.

If MSBuild prints repeated red lines from
`vcpkg\scripts\buildsystems\msbuild\applocal.ps1` with `The system cannot find
the path specified`, reconfigure with:

```powershell
cmake --preset windows-vs2022-pro
```

That failure usually means vcpkg cached a stale Microsoft Store `pwsh.exe`
version path. The Windows preset pins vcpkg app-local copying to stable Windows
PowerShell.

If CMake cannot find Qt on Apple Silicon, confirm that Homebrew Qt 5 is
installed at `/opt/homebrew/opt/qt@5` and rerun the configure preset.

## MF4/MDF Files on macOS

The MF4 loader uses Python 3 and the `asammdf` package. Install it into a
dedicated virtual environment:

```sh
python3 -m venv "$HOME/.venvs/plotjuggler-mf4"
"$HOME/.venvs/plotjuggler-mf4/bin/python" -m pip install --upgrade pip asammdf
```

Set `PJ_ASAMMDF_PYTHON` to that environment's Python executable before
launching PlotJuggler:

```sh
export PJ_ASAMMDF_PYTHON="$HOME/.venvs/plotjuggler-mf4/bin/python"
./install/macos-arm64/bin/plotjuggler
```

## Map Tiles

The app does not default to `tile.openstreetmap.org`. Set a provider before
launching if online map tiles are needed:

```powershell
$env:PJ_MAP_TILES_URL="https://your.tile.server/{z}/{x}/{y}.png"
$env:PJ_MAP_ATTRIBUTION="Your attribution text"
.\install\bin\plotjuggler.exe
```

On macOS, set `PJ_MAP_TILES_URL` and `PJ_MAP_ATTRIBUTION` in the shell before
launching the app.

## Current Feature Surface

- Native `Add Map View` split/dock panels
- XY-only `Convert to Map panel`
- Map layout state persisted in dock XML
- Map marker updates during tracker movement and playback
- Simple latitude/longitude autodetection using `Latitude` and `Longitude`
- One master time-series column per tab
- Shared X viewport across non-XY time-series plots in a tab
- X-only time-series navigation with automatic Y refit
- `PlotDocker`-owned Y-axis/canvas alignment
- Per-tab timeline slider following the visible time viewport
- Lazy-loading data work, including MF4 paths
