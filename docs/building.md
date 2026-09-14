# Building

You must have a 64-bit machine for building and running the project. Always
run your system updater before building and make sure you have the latest
drivers.

## Setup

### Windows

* Windows 10 or later
* [Visual Studio 2022 or later](https://www.visualstudio.com/downloads/),
  or [CLion](https://www.jetbrains.com/clion/) with the standalone [Build Tools](#build-tools-only-no-visual-studio-ide)
  (compiler + SDK, no IDE needed)
* CMake 3.10+ (or C++ CMake tools for Windows)
* Windows 11 SDK version 10.0.22000.0 (for Visual Studio 2022, this or any newer version)
* [Python 3.6+ 64-bit](https://www.python.org/downloads/)
  * Ensure Python is in PATH.
* [Vulkan SDK](https://sdk.lunarg.com/sdk/download/latest/windows/vulkan-sdk.exe)
  * The build script will automatically detect it if installed at `C:\VulkanSDK`

```
git clone https://github.com/xenia-canary/xenia-canary.git
cd xenia-canary
xb setup

# Build on command line (add --config=release for release):
xb build


# Pull latest changes, rebase, update submodules, and run premake:
xb pull

# Run premake and open Visual Studio (run the 'xenia-app' project):
xb devenv

# Same, but open CLion instead (needs 'clion'/'clion64' on PATH,
# otherwise open the project folder in CLion manually):
xb devenv --ide=clion

# Run premake to update the sln/vcproj's:
xb premake

# Format code to the style guide:
xb format
```

#### Build Tools only (no Visual Studio IDE)

You don't need the Visual Studio IDE — the standalone Build Tools provide
the MSVC compiler and Windows SDK that `xb` and CLion use:

1. Download **Build Tools for Visual Studio** (2026 or 2022) from the
   [Visual Studio downloads page](https://visualstudio.microsoft.com/downloads/)
   (under "Tools for Visual Studio") and run the installer as admin.
2. Select the **Desktop development with C++** workload.
3. Under "Installation details", make sure these are checked (most come with
   the workload or its Recommended set):
   * MSVC C++ x64/x86 build tools (v145 for VS 2026, v143 for VS 2022)
   * Windows 11 SDK (any recent version — includes `fxc.exe` for shaders)
   * C++ CMake tools for Windows (provides CMake + Ninja for `xb build`)
   * C++ AddressSanitizer (required for `xb build --config=checked`)
   * Only if targeting ARM64: MSVC C++ ARM64 build tools
4. Install, then verify the C++ workload actually landed: the
   `VC\Tools\MSVC` folder should exist under the install path (default:
   `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools` for 2026).
   If it's missing, the `--add` flags were dropped — re-run the install
   command. Then open a fresh terminal and run `xb setup` — `xb` locates the
   Build Tools automatically, so the install location doesn't matter.

Command-line alternatives (admin terminal):

```
:: VS 2026 Build Tools
winget install --id Microsoft.VisualStudio.BuildTools -e --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"

:: VS 2022 Build Tools
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

If winget reports the package already installed with no upgrade available,
it skips the installer and the workload is never added. Modify the existing
install instead (admin terminal):

```
& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vs_installer.exe" modify --installPath "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools" --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --norestart
```
(Adjust the `--installPath` for VS 2022: `...\2022\BuildTools`.)

CLion picks up a Build Tools install as a "Visual Studio" toolchain on its
own — nothing extra to configure beyond selecting it.

Switching between the VS IDE and Build Tools (or a VS update changing the
MSVC version directory) can orphan the compiler path in
`build/CMakeCache.txt` — `xb` detects that and clears the cache
automatically on the next configure.

If CLion fails to reload with "Cannot locate vcvarsall.bat" after removing
a VS installation, its cached toolchain is stale: Settings > Build >
Toolchains > remove the old Visual Studio entry so it redetects the current
install (`xb devenv --ide=clion` warns about this automatically).
CLion does not always redetect a new installation by itself — if reload then
reports "Toolchain 'Visual Studio' is not found", re-add it with the `+`
button (type Visual Studio, and name it exactly `Visual Studio`), then
reload.
<!--
# Remove intermediate files and build outputs (doesn't work on Linux):
xb clean

# Check for lint errors with clang-format:
xb lint

# Run the style checker on all code:
xb style

# Remove all build/ output and do a hard git reset:
xb nuke

# Runs the clang-tidy checker on all code:
xb tidy


## Testing:

# Generate tests:
xb gentests

# Run tests:
xb test

# Run GPU tests:
xb gputest


## Other:

# Generate SPIR-V binaries and header files:
xb genspirv
-->

#### CLion (Windows)

`xb devenv --ide=clion` runs the normal Ninja CMake configure (`build/`,
`default` preset) and opens the project folder in CLion. If the launcher is
not on PATH, open the folder manually.

In CLion: select the `default` CMake preset — it pins the Visual Studio
toolchain automatically (via `vendor.jetbrains.com/clion` in
`CMakePresets.json`), so no toolchain setup is needed. Then build/run the
`xenia-app` target with the working directory set to the repo root.
If your VS toolchain is named something other than "Visual Studio", select
it once under Settings > Build > Toolchains.
The MinGW toolchain is not supported on Windows — this project requires
MSVC, so keep the `default` profiles on the Visual Studio toolchain.

#### Debugging

VS behaves oddly with the debug paths. Open the 'xenia-app' project properties
and set the 'Command' to `$(SolutionDir)$(TargetPath)` and the
'Working Directory' to `$(SolutionDir)..\..`. You can specify flags and
the file to run in the 'Command Arguments' field (or use `--flagfile=flags.txt`).

By default logs are written to a file with the name of the executable. You can
override this with `--log_file=log.txt`.

If running under Visual Studio and you want to look at the JIT'ed code
(available around 0xA0000000) you should pass `--emit_source_annotations` to
get helpful spacers/movs in the disassembly.

### Linux

Linux support is extremely experimental and presently incomplete.

The build script uses Clang 19. GCC while it should work in theory, is not easily
interchangeable right now.

* Normal building via `xb build` uses CMake+Ninja.
* Environment variables:
  Name  | Default Value
  ----- | -------------
  `CC`  | `clang`
  `CXX` | `clang++`

<!--* [CodeLite](https://codelite.org) is supported. `xb devenv` will generate a workspace and attempt to open it. Your distribution's version may be out of date so check their website.
* Experimental CMake generation is available to facilitate use of other IDEs such as [CLion](https://www.jetbrains.com/clion/). If `clion` is available inside `$PATH`, `xb devenv` will start it. Otherwise `build/CMakeLists.txt` needs to be generated by invoking `xb premake --devenv=cmake` manually.-->

Clang-19 or newer should be available from system repositories on all up to date distributions.
You will also need some development libraries. To get them on an Ubuntu system:

```sh
sudo apt-get install build-essential mesa-vulkan-drivers valgrind libc++-dev libc++abi-dev libgtk-3-dev liblz4-dev libsdl2-dev libvulkan-dev libx11-xcb-dev clang-19 llvm-19 ninja-build
```

In addition, you will need up to date Vulkan libraries and drivers for your hardware, which most distributions have in their standard repositories nowadays.

**Vulkan SDK (for shader compilation)**

The build uses `spirv-opt` and `glslangValidator` for shader compilation. Check
if your system version is recent enough:

```sh
spirv-opt --version
```

If the version is older than 2026.1, the system `spirv-tools` package will not
support all required options (e.g. `--canonicalize-ids`). In that case, install
the [Vulkan SDK from LunarG](https://vulkan.lunarg.com/sdk/home) and set
`VULKAN_SDK` so the build picks up the correct tools:

```sh
wget -qO vulkan-sdk.tar.xz https://sdk.lunarg.com/sdk/download/latest/linux/vulkan-sdk.tar.xz
mkdir -p ~/vulkan-sdk
tar -xf vulkan-sdk.tar.xz -C ~/vulkan-sdk
rm vulkan-sdk.tar.xz
export VULKAN_SDK=$HOME/vulkan-sdk/$(ls ~/vulkan-sdk)/x86_64
export PATH="$VULKAN_SDK/bin:$PATH"
```

Add the `export` lines to your shell profile to persist them across sessions.

## Running

To make life easier you can set the program startup arguments in your IDE to something like `--log_file=stdout /path/to/Default.xex` to log to console rather than a file and start up the emulator right away.
