# PlotJugglerPro Local Build

How to build and run PlotJugglerPro on a development machine. For packaging
and publishing a release, see `PLOTJUGGLER_PRO_RELEASE.md`.

Maintained platforms:

- Windows 11, Visual Studio 2022, preset `windows-vs2022-pro`
- Native Apple Silicon macOS, Homebrew Qt 5, preset `macos-arm64-pro`

Linux, Snap, AppImage, Nix, Docker, ROS package and Conan workflows are
inherited from upstream and are not maintained for PlotJugglerPro. Don't use
old upstream commands that clone `facontidavide/PlotJuggler` or build in
`build/PlotJuggler`.

## Requirements

### Windows

- Visual Studio 2022
- CMake
- vcpkg at `C:\vcpkg`
- Qt 5.15.2 at `C:\Qt\5.15.2\msvc2019_64`
- Qt Installer Framework 4.10, only when creating an installer

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

## Windows

Configure (writes to `build\PlotJugglerPro`):

```powershell
cmake --preset windows-vs2022-pro
```

Build the runnable local install:

```powershell
cmake --build build\PlotJugglerPro --config Release --target install
```

or the equivalent preset:

```powershell
cmake --build --preset windows-vs2022-pro-release-install
```

Run:

```powershell
.\install\bin\plotjuggler.exe
```

Fast compile check without installing:

```powershell
cmake --build build\PlotJugglerPro --config Release --target plotjuggler
```

## macOS Apple Silicon

Configure, build and install the native arm64 executable and plugins:

```sh
cmake --preset macos-arm64-pro
cmake --build --preset macos-arm64-pro-release-install
```

Run:

```sh
./install/macos-arm64/bin/plotjuggler
```

Fast compile check without installing:

```sh
cmake --build --preset macos-arm64-pro-release --target plotjuggler
```

This local executable links against Homebrew Qt and only runs on a machine set
up like this one. To build the relocatable `.app` and DMG you give to users,
see `PLOTJUGGLER_PRO_RELEASE.md`.

## Build Options

- `PJ_UPDATE_REPO`: GitHub `owner/repo` that the update check queries. The
  default is `terps-racing-ev/PlotJugglerPro`; set it empty to disable the
  check. Example:
  `cmake --preset windows-vs2022-pro -DPJ_UPDATE_REPO=`. It is a cache
  variable, so it stays set until you configure again with another value.

## Troubleshooting

If `install\bin\plotjuggler.exe` is running, the install step fails with
"Permission denied" because Windows will not overwrite a running executable.
Close PlotJugglerPro and rebuild.

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

The release DMG bundles Python and `asammdf`, so MF4 files work there without
setup. Local development builds use `python3` by default. Install `asammdf`
into that environment, or set `PJ_ASAMMDF_PYTHON` to another Python
executable:

```sh
python3 -m venv "$HOME/.venvs/plotjuggler-mf4"
"$HOME/.venvs/plotjuggler-mf4/bin/python" -m pip install --upgrade pip asammdf
export PJ_ASAMMDF_PYTHON="$HOME/.venvs/plotjuggler-mf4/bin/python"
./install/macos-arm64/bin/plotjuggler
```

Packaged apps use their bundled Python automatically. `PJ_ASAMMDF_PYTHON` still
overrides it for troubleshooting.

## Map Tiles

The map panel is a native Qt widget and does not need Qt WebEngine. The app
does not default to `tile.openstreetmap.org`. Set a tile provider before
launching if you need online map tiles:

```powershell
$env:PJ_MAP_TILES_URL="https://your.tile.server/{z}/{x}/{y}.png"
$env:PJ_MAP_ATTRIBUTION="Your attribution text"
.\install\bin\plotjuggler.exe
```

On macOS, export the same two variables in the shell before launching.

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
- Runs panel and Compare tabs
- Lazy-loading data work, including MF4 paths
- GitHub release update check (startup toast and Help > Check for Updates...)
