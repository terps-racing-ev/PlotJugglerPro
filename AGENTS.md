# PlotJugglerPro Agent Notes

## Operating Rules

- Do not use the Windows sandbox. It fails on this workspace and wastes time.
  Run needed commands with escalation when the sandbox runner fails.
- This is a beta workflow branch. Prefer deleting stale behavior and stale docs
  over preserving upstream compatibility by default.
- Keep changes focused, but do not protect cruft. If a thing is not part of the
  current Windows workflow or planned macOS workflow, call it out or remove it.
- Do not treat `plotjuggler_base` as legacy. It is the core library used by the
  Pro app and plugins.

## Current Product Direction

Expected usage is small and occasional: roughly 10-20 people using the app
intermittently. Optimize decisions for a practical internal/beta workflow, not
mass-public scale, high-volume hosted services, or broad anonymous distribution.

PlotJugglerPro is a local data-review workflow fork with:

- native dock/split map panels,
- lazy-loading data support, including MF4 work,
- x-only navigation for time-series plots,
- auto-Y refit behavior,
- one master time-series column per tab,
- deterministic Y-axis/canvas alignment across stacked time-series plots,
- a per-tab timeline slider aligned under the time-series canvas.

## Supported Build Paths

Windows local build:

```powershell
cmake --preset windows-vs2022-pro
cmake --build build\PlotJugglerPro --config Release --target install
.\install\bin\plotjuggler.exe
```

Fast compile check:

```powershell
cmake --build build\PlotJugglerPro --config Release --target plotjuggler
```

Installer:

```powershell
.\tools\package_windows_release.ps1
```

If MSBuild emits repeated red lines from
`vcpkg\scripts\buildsystems\msbuild\applocal.ps1`, re-run
`cmake --preset windows-vs2022-pro`. The preset pins vcpkg's app-local copy step
to stable Windows PowerShell so Microsoft Store `pwsh.exe` version changes do
not break generated projects.

Native Apple Silicon macOS build:

```sh
brew install cmake qt@5
cmake --preset macos-arm64-pro
cmake --build --preset macos-arm64-pro-release-install
./install/macos-arm64/bin/plotjuggler
```

The macOS preset targets arm64, expects Homebrew at `/opt/homebrew` with Qt 5
at `/opt/homebrew/opt/qt@5`, and explicitly adds the Command Line Tools SDK
libc++ headers. It produces a local build, not a relocatable `.app` bundle or
DMG.

Do not use inherited Snap, AppImage, Nix, Docker, ROS package, or Conan build
instructions unless the task is explicitly to revive that platform.

## Architecture

- `plotjuggler_base`: core data model, plugin API, plot widget base, Qwt helpers,
  transforms, serializers.
- `plotjuggler_app`: GUI executable and PlotJugglerPro workflow logic.
- `plotjuggler_plugins`: loaders/parsers/tools. Keep plugins that support the
  workflow; prune unused upstream plugins deliberately.
- `installer` and `tools/package_windows_release.*`: current Windows release path.

## Time-Series Layout Model

- A `PlotDocker` tab owns one master time viewport for time-series plots.
- Non-XY time-series plots in the same tab must always share the same X range.
- Time-series plots are added vertically into the master column.
- Right-side panels are for XY plots or map views only.
- Adding a time-series signal preserves the tab X viewport and recalculates that
  plot's Y range.
- The first time-series signal in a fresh tab initializes the tab viewport from
  the full data range.
- Y-axis alignment is handled by `PlotDocker`, not by individual plots.
- The old zoom link button and linked-zoom fanout were removed. Do not
  reintroduce `buttonLink`, `linkedZoomOut()`, or per-plot linked-zoom opt-outs.

## Timeline and Tracker

- The timeline slider is per `PlotDocker` tab, not the old global main-window
  slider.
- `MainWindow` owns absolute tracker/playback time.
- `PlotDocker` stores the time viewport in plot-relative coordinates and exposes
  slider/playback bounds in absolute time by adding the plot time offset.
- Tracker movement, playback, and the tab timeline slider must stay in sync.
- After adding signals or changing layout/axis geometry, refresh shared time axes
  and tracker position so the vertical now line updates immediately.

## Run Compare

- A run (lap) is a manual time window in absolute time, owned by the
  `RunSet::instance()` singleton (`run_set.h`). Runs are saved as `<runs>` in the
  layout/undo XML and loaded before tabs.
- Runs are created only from the Runs panel under the curve tree: `Mark Start` /
  `Mark End` (or `[` / `]`) at the tracker. Creating a run from the visible range
  was removed on purpose; do not reintroduce it. Runs are deleted from the
  table's right-click menu or with the Delete key.
- Normal time-series plots draw included runs as background bands
  (`RunBandItem`).
- A Compare tab is a `PlotDocker` with compare mode set (`compare="true"` on
  `<Tab>`). Its plots store logical signals and expand each one into one
  `RunWindowTimeseries` curve per included run, with x in seconds since run start.
  Only the signals are saved in XML.
- Compare tabs use a time offset of 0, keep their own tracker and slider, and
  never drive `MainWindow` tracker time, the global time offset, or map panels
  in other tabs. XY plots, transforms and the curve editor are not offered there.
- A map panel in a Compare tab runs in compare mode (`MapDockPanel::setCompareMode`):
  one marker per included run in the run's color, plus a trail for the reference
  run only, driven by the tab's run-relative tracker (`onRunTimeUpdated`). It
  ignores global tracker time.
- Color identifies a run everywhere. The reference run is drawn thicker and on
  top. A plot's second and later signals are drawn dashed. Every per-run curve
  is listed in the legend as `signal [run]`, with a line icon showing its style.

## Map Panel

- Map is a native dock/split panel in the main plot layout, not a toolbox window.
- Plot context menu includes `Add Map View` and XY-only `Convert to Map panel`.
- Map panel state, including latitude/longitude selections, is persisted in dock
  XML and restored with layouts.
- Map receives tracker/playback time updates and moves the marker in sync.
- Latitude/longitude autodetection is intentionally simple: keyword match for
  `Latitude` and `Longitude` only.
- `Fit to View` refreshes data, runs detection, and fits the route.
- Right-click inside the native Qt map widget is forwarded to app code.
- The default packaged map can use USGS The National Map imagery/topo tiles
  because expected usage is small, occasional, and US-focused.
- Do not default app code to `tile.openstreetmap.org`; configure tiles with
  `PJ_MAP_TILES_URL` and `PJ_MAP_ATTRIBUTION`.

## Key Files

- `plotjuggler_app/plot_docker.h`
- `plotjuggler_app/plot_docker.cpp`
- `plotjuggler_app/plotwidget.h`
- `plotjuggler_app/plotwidget.cpp`
- `plotjuggler_app/mainwindow.cpp`
- `plotjuggler_app/mainwindow.ui`
- `plotjuggler_app/realslider.h`
- `plotjuggler_app/map_dock_panel.h`
- `plotjuggler_app/map_dock_panel.cpp`
- `plotjuggler_app/CMakeLists.txt`
- `plotjuggler_base/src/plotwidget_base.cpp`
