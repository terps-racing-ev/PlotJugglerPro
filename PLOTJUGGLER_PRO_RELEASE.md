# PlotJugglerPro Releases

How to cut a PlotJugglerPro release for Windows and macOS and publish it so
the app's update check finds it.

This is a small internal workflow: packaging runs locally on a Windows PC and
an Apple Silicon Mac, and releases are published by hand on GitHub. GitHub
Actions and the inherited upstream release paths are not maintained.

For setting up a development build, see `PLOTJUGGLER_PRO_BUILD.md`.

## Release Checklist

1. Commit everything that should ship. The working tree should be clean.
2. Pick the tag: today's date as `YYYY.M.D` (e.g. `2026.11.19`). For a second
   release on the same day, add one letter: `2026.11.19a`, then `2026.11.19b`.
   Use nothing else after the date (`-rc1`, `-beta`), because the update check
   does not recognize it.
3. Tag and push:

   ```powershell
   git tag 2026.11.19
   git push origin main 2026.11.19
   ```

4. Package on each platform you are shipping (see below). Tag **before**
   packaging: the build bakes in the nearest tag, and the package filename comes
   from it too.
5. Smoke test the installer or DMG.
6. Publish the GitHub Release (see below).

## How the App Knows Its Version

Every build runs `cmake/pj_release_tag.cmake`, which takes the nearest tag from:

```sh
git describe --tags --abbrev=0 --match "[0-9]*"
```

and writes it into `build/.../plotjuggler_app/generated/pj_release_tag.h`. The
About dialog shows it, and the update check compares it with the latest GitHub
Release.

- A build a few commits past a tag reports that tag. Uncommitted changes are
  not reflected.
- A build with no tag (no git, or no tags) is treated as a dev build and never
  checks for updates on startup.
- The `3.15.0` shown as "core" in About is the inherited CMake project version,
  not the release version.

## Windows Installer

From PowerShell on the Windows build machine:

```powershell
.\tools\package_windows_release.ps1
```

Or from Git Bash:

```bash
bash ./tools/package_windows_release.sh
```

Close any running `install\bin\plotjuggler.exe` first.

Output, in the repository root:

```text
PlotJugglerPro-2026-11-19-Windows-x64-Installer.exe
```

The name comes from `git describe --tags --always --dirty` with dots turned
into hyphens. A dirty working tree or commits after the tag add a suffix such
as `-3-gabc1234-dirty`. If you see that, commit or re-tag before publishing.

The script:

1. Builds the Release `plotjuggler` target.
2. Stages `build\PlotJugglerPro\bin\Release` into
   `installer\io.plotjuggler.application\data`.
3. Removes stale version-named executables such as `plotjuggler-3.15.exe` and
   renames the app executable to `PlotJugglerPro.exe`.
4. Removes Qt runtime DLLs from the payload, runs
   `C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe`, then restores the
   plugin-needed Qt DLLs (such as `Qt5WebSockets.dll`) from the same Qt
   install, so Qt builds are never mixed.
5. Copies license texts into the installed `licenses` folder.
6. Validates key runtime files (`platforms\qwindows.dll`, key Qt DLLs).
7. Writes the version into `installer\config.xml` and
   `installer\io.plotjuggler.application\meta\package.xml`.
8. Runs `C:\Qt\Tools\QtInstallerFramework\4.10\bin\binarycreator.exe`.

Step 7 leaves `installer\config.xml` and `package.xml` modified. Those changes
come from packaging, so you don't need to commit them.

Distribute the generated installer, never the raw `install\bin\plotjuggler.exe`:
that only works on a machine that already has Qt, vcpkg and the build paths.

### What the installer does

- Installs to `C:\Program Files\PlotJugglerPro`
  (`<TargetDir>@ApplicationsDirX64@/PlotJugglerPro</TargetDir>`).
- Creates `Start Menu\PlotJugglerPro\PlotJugglerPro.lnk` and
  `Desktop\PlotJugglerPro.lnk`.
- Has no license agreement page. License texts go to
  `C:\Program Files\PlotJugglerPro\licenses`.
- Uses `installer\installer.qss`, a plain light stylesheet, to avoid
  white-on-white text on some Windows themes.

Names, folders and shortcuts are set in `installer\config.xml` and
`installer\io.plotjuggler.application\meta\package.xml`.

## macOS DMG

On an Apple Silicon Mac with the build tools from `PLOTJUGGLER_PRO_BUILD.md`,
plus `uv`:

```sh
brew install uv
./tools/package_macos_release.sh
```

Output:

```text
dist/macos-arm64/PlotJugglerPro-2026-11-19-macOS-arm64.dmg
```

The script builds the Release app and plugins, embeds Qt and its platform
plugins, bundles a relocatable Python runtime with `asammdf` for MF4 loading
(it needs network access to fetch them), and signs the app ad hoc. The app
targets macOS 13 Ventura or newer and uses the PlotJuggler SVG artwork for its
icon.

`PJ_MF4_PYTHON_VERSION` and `PJ_ASAMMDF_VERSION` override the bundled Python
minor version and the pinned `asammdf` version.

The ad-hoc signature is free and needs no Apple Developer account, but the app
is not signed with a developer identity or notarized. Gatekeeper may block the
first launch. Users can right-click the app in Finder and choose **Open**, or,
after verifying where it came from:

```sh
xattr -dr com.apple.quarantine /Applications/PlotJugglerPro.app
```

## Smoke Test

Windows:

1. Uninstall any existing PlotJugglerPro from Windows Apps & Features.
2. Run the installer and confirm the default path is
   `C:\Program Files\PlotJugglerPro`.
3. Launch PlotJugglerPro and check Help > About shows the new tag.
4. Load a normal data file and an MF4 file.
5. Try the features users depend on: map panels, timeline slider, tracker sync,
   stacked time-series axis alignment, Runs/Compare.

macOS: open the DMG, drag the app to Applications, launch it, and repeat steps
3–5.

If possible, test on a second machine or a clean VM.

## Publish the GitHub Release

1. Go to <https://github.com/terps-racing-ev/PlotJugglerPro/releases/new>.
2. Choose the tag you pushed and use the tag as the title. "Generate release
   notes" gives a commit summary to edit.
3. Attach the files **without renaming them**:
   - `PlotJugglerPro-*-Windows-x64-Installer.exe`
   - `PlotJugglerPro-*-macOS-arm64.dmg`
4. Leave "Set as a pre-release" unchecked, keep "Set as the latest release"
   checked, and publish.

With the `gh` CLI:

```powershell
gh release create 2026.11.19 PlotJugglerPro-2026-11-19-Windows-x64-Installer.exe `
  -R terps-racing-ev/PlotJugglerPro --title 2026.11.19 --generate-notes --latest
# add the DMG later, from the Mac:
gh release upload 2026.11.19 dist/macos-arm64/PlotJugglerPro-2026-11-19-macOS-arm64.dmg -R terps-racing-ev/PlotJugglerPro
```

Release attachments are stored outside the git repository, so they do not make
clones larger.

### Rules the update check depends on

- It queries GitHub's `releases/latest`, which ignores drafts and pre-releases.
- "Latest" is the most recently **created** release unless one is explicitly
  marked latest. When backfilling old releases, create them oldest first or
  mark the newest as latest.
- The Download link goes straight to the attachment whose name ends in
  `-Windows-x64-Installer.exe` or `-macOS-arm64.dmg`. If none matches, it opens
  the release page instead.
- The repository checked is the CMake cache variable `PJ_UPDATE_REPO` (default
  `terps-racing-ev/PlotJugglerPro`). Setting it empty disables the check.

## How Users Update

About 2 seconds after launch, the app checks GitHub. If a newer release
exists, it shows a toast with **Download** and **Release notes** links. Users
can also use Help > Check for Updates..., which always reports a result and
offers **Skip This Version**. The startup check can be turned off in
Preferences.

To install the update:

- **Windows:** close PlotJugglerPro, uninstall the old version from Windows
  Apps & Features, then run the new installer. Keeping one install directory
  makes troubleshooting simpler.
- **macOS:** open the DMG and drag the app into Applications, replacing the old
  one.

Builds older than `2026.11.17a` have no update check, and `2026.11.17a` itself
never checks on startup. Users on those builds need to install the next
release by hand once.

## Appendix: Manual Windows Packaging

`package_windows_release.ps1` does all of this. These steps are only for
troubleshooting the script or rebuilding the process by hand. Run them from
the repository root.

1. Build:

   ```powershell
   cmake --build build\PlotJugglerPro --config Release --target plotjuggler
   ```

2. Stage installer files:

   ```powershell
   Remove-Item -Recurse -Force installer\io.plotjuggler.application\data -ErrorAction SilentlyContinue
   New-Item -ItemType Directory -Force -Path installer\io.plotjuggler.application\data
   Copy-Item -Path build\PlotJugglerPro\bin\Release\* -Destination installer\io.plotjuggler.application\data -Recurse -Force
   Remove-Item installer\io.plotjuggler.application\data\plotjuggler-*.exe -Force -ErrorAction SilentlyContinue
   Move-Item installer\io.plotjuggler.application\data\plotjuggler.exe installer\io.plotjuggler.application\data\PlotJugglerPro.exe -Force
   New-Item -ItemType Directory -Force -Path installer\io.plotjuggler.application\data\licenses
   Copy-Item -Path installer\io.plotjuggler.application\meta\license_*.txt -Destination installer\io.plotjuggler.application\data\licenses -Force
   ```

3. Deploy Qt runtime files (pass the full `windeployqt.exe` path if it is not
   on `PATH`):

   ```powershell
   cmd /c installer\windeploy_pj.bat C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe
   ```

4. Create the offline installer. If `binarycreator.exe` is elsewhere, find it
   with
   `Get-ChildItem C:\Qt -Recurse -Filter binarycreator.exe | Select-Object -First 1 FullName`.

   ```powershell
   & "C:\Qt\Tools\QtInstallerFramework\4.10\bin\binarycreator.exe" --offline-only -c installer\config.xml -p installer PlotJugglerPro-2026-11-19-Windows-x64-Installer.exe
   ```

   The last argument only sets the output filename. Keep the
   `-Windows-x64-Installer.exe` ending so the update check's Download link
   finds it.
