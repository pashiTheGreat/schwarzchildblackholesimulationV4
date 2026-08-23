# Schwarzschild Black Hole Simulation

A real-time C++ and OpenGL simulation of a Schwarzschild black hole. Photon
paths are integrated in a GPU compute shader to render gravitational lensing,
the event-horizon silhouette, an emissive accretion disk, orbiting objects, and
a procedurally generated deep-space background.

## Features

- GPU ray tracing with an OpenGL 4.3 compute shader
- Schwarzschild gravitational lensing
- Procedural stars, galactic clouds, and nebula-like background detail
- Relativistic accretion disk with Doppler beaming and gravitational redshift
- Photon-ring and higher-order disk-image enhancement
- Interactive orbit camera and zoom
- Optional gravitational motion for scene objects
- NVIDIA Optimus and AMD high-performance GPU hints on Windows
- Separate 2D lensing demonstration

## Requirements

- A GPU and driver supporting **OpenGL 4.3 or newer**
- A C++17 compiler
- [CMake 3.21+](https://cmake.org/download/)
- [Git](https://git-scm.com/downloads)
- One of the dependency setups below

The 3D simulation is GPU intensive. Update the graphics driver before
troubleshooting rendering or compute-shader errors.

> **Important:** Do not run the legacy `black_hole.exe` located in the
> repository root. Build and run the `BlackHole3D` target using the steps
> below. CMake places the required shader files beside the built executable.

## Windows: build with vcpkg

These commands work in PowerShell. Visual Studio 2022 with the **Desktop
development with C++** workload is the recommended compiler setup.

### 1. Clone this repository

```powershell
git clone https://github.com/pashiTheGreat/schwarzchild-black-hole-simulation.git
cd schwarzchild-black-hole-simulation
```

### 2. Install vcpkg

If vcpkg is not already installed:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

### 3. Configure the project

The repository contains `vcpkg.json`, so vcpkg installs GLFW, GLEW, and GLM
automatically during configuration.

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

If vcpkg is installed somewhere else, replace `C:/vcpkg` with its actual path.

### 4. Build the 3D simulation

```powershell
cmake --build build --config Release --target BlackHole3D
```

### 5. Launch it

```powershell
.\build\Release\BlackHole3D.exe
```

Keep the working directory at `build\Release` when launching. That directory
contains `geodesic.comp`, `grid.vert`, and `grid.frag` copied by CMake.

## Ubuntu/Debian: build with system packages

### 1. Install the compiler and dependencies

```bash
sudo apt update
sudo apt install build-essential cmake git \
  libglew-dev libglfw3-dev libglm-dev libgl1-mesa-dev
```

### 2. Clone, configure, and build

```bash
git clone https://github.com/pashiTheGreat/schwarzchild-black-hole-simulation.git
cd schwarzchild-black-hole-simulation
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel --target BlackHole3D
```

### 3. Launch it

```bash
cd build
./BlackHole3D
```

Linux support depends on the installed OpenGL driver. Proprietary NVIDIA/AMD
drivers may provide better compute-shader performance than fallback Mesa
drivers on some systems.

## Controls

| Input | Action |
| --- | --- |
| Left-click and drag | Orbit around the black hole |
| Middle-click and drag | Orbit around the black hole |
| Mouse wheel | Zoom in or out |
| Hold right mouse button | Enable object gravity while held |
| `G` | Toggle object gravity on or off |
| `I` / `Shift+I` | Increase/decrease disk inclination |
| `T` / `Shift+T` | Increase/decrease disk thickness |
| Window close button | Exit the simulation |

## Verify which GPU is being used

At startup, the console prints lines similar to:

```text
OpenGL vendor: NVIDIA Corporation
OpenGL renderer: NVIDIA GeForce RTX 3050 Ti Laptop GPU/PCIe/SSE2
OpenGL version: 4.3.0 NVIDIA ...
```

The `OpenGL renderer` line is the authoritative adapter used by the
simulation.

On a Windows laptop with hybrid graphics, the executable requests the
high-performance GPU automatically. If it still reports Intel or AMD
integrated graphics:

1. Open **Settings > System > Display > Graphics**.
2. Select **Browse** and choose `build\Release\BlackHole3D.exe`.
3. Open **Options**, select **High performance**, and save.
4. Close every running simulation window and launch it again.

For NVIDIA GPUs, `nvidia-smi pmon -c 1` can also show `BlackHole3D.exe` while
the simulation is running.

## Build and run the 2D demonstration

Windows:

```powershell
cmake --build build --config Release --target BlackHole2D
.\build\Release\BlackHole2D.exe
```

Linux:

```bash
cmake --build build --parallel --target BlackHole2D
./build/BlackHole2D
```

## Troubleshooting

### `Failed to create GLFW window`

Update the GPU driver and confirm that the GPU supports OpenGL 4.3.

### `Failed to open compute shader: geodesic.comp`

Launch the executable from its CMake output directory. Do not move the
executable without also copying `geodesic.comp`, `grid.vert`, and `grid.frag`.

### The integrated GPU is used on a laptop

Follow the Windows Graphics preference steps in the GPU verification section.
The preference is stored for the exact executable path, so it may need to be
set again after moving the build directory.

### The window is slow while moving the camera

The simulation integrates many geodesic steps per pixel and can fully utilize
a GPU. Build in `Release` mode, close other GPU-heavy applications, and use the
latest graphics driver.

## Project layout

| File | Purpose |
| --- | --- |
| `black_hole.cpp` | 3D application, camera, OpenGL setup, and GPU dispatch |
| `geodesic.comp` | Schwarzschild geodesics, lensing, disk, and starfield |
| `grid.vert`, `grid.frag` | Spacetime grid shaders |
| `2D_lensing.cpp` | Standalone 2D lensing demonstration |
| `CMakeLists.txt` | Portable build targets and shader copying |
| `vcpkg.json` | GLFW, GLEW, and GLM dependency manifest |

## Tested configuration

The current 3D build has been tested on Windows with:

- NVIDIA GeForce RTX 3050 Ti Laptop GPU
- OpenGL 4.3
- NVIDIA driver 595.97
- C++17 MinGW build

Other OpenGL 4.3-capable systems should be able to build and run it using the
instructions above, but hardware and driver combinations can behave
differently. Please include the printed OpenGL vendor, renderer, and version
when reporting a problem.
