# Validated Schwarzschild Geodesic Renderer

This project is a C++17/OpenGL scientific visualization of null geodesics
around a nonrotating, uncharged, isolated Schwarzschild black hole. Its vacuum
ray paths and shadow geometry are validated against an independent CPU
reference and analytical results. The luminous accretion disk is an
approximate thin-disk emitter, not a plasma or radiative-transfer simulation.

The default is **Physical** mode. **Cinematic** mode is explicitly artistic.

## What is—and is not—modeled

| Feature | Physical | Cinematic |
| --- | --- | --- |
| Null Schwarzschild geodesics | Validated GPU integration | Same integration |
| Horizon capture and lensed background | Yes | Yes |
| Thin-disk flux and effective temperature | Approximate zero-torque model | Used as a base |
| Observer/emitter frequency shift and bolometric `g^4` | Yes | Yes |
| Fixed orange grading | No | Yes |
| Artificial photon-ring emission | No | Yes |
| Higher-order brightness enhancement | No | Yes |
| Aggressive filmic tone mapping | No | Yes |

There is no Kerr spin or frame dragging, charged metric, dynamical spacetime,
orbiting-object simulation, magnetohydrodynamics, full spectrum, or complete
radiative transfer. See [PHYSICS_VALIDATION.md](PHYSICS_VALIDATION.md) for the
equations, conventions, tolerances, evidence, and limitations.

## Requirements

- Windows 10/11 and a GPU driver supporting OpenGL 4.3 or newer
- Visual Studio with the Desktop development with C++ workload
- CMake 3.21 or newer
- Git and [vcpkg](https://github.com/microsoft/vcpkg)

The verified configuration is Visual Studio 18 (MSVC 19.50), Windows SDK
10.0.26100.0, vcpkg x64-windows, and an NVIDIA GeForce RTX 3050 Ti Laptop GPU
using OpenGL 4.3 / driver 595.97. Other platforms and drivers are not claimed
as verified by the current report.

## Clean Windows build

Run these commands in PowerShell from the repository root. If vcpkg is not
already installed, install it once:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

Configure a fresh build. The manifest installs GLEW, GLFW, GLM, and ImGui:

```powershell
cmake -S . -B build `
  -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

Build and test both configurations:

```powershell
cmake --build build --config Debug
cmake --build build --config Release
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

`ctest` runs the dependency-free CPU reference without opening a GLFW window.

## Run

Shaders are copied beside `BlackHole3D.exe`; use that directory as the working
directory:

```powershell
Set-Location build\Release
.\BlackHole3D.exe
```

At startup, the application prints the OpenGL vendor, renderer, and version.
The renderer line identifies the GPU actually used.

The historical 2D visualization remains a separate, non-validation target:

```powershell
.\BlackHole2D.exe
```

## Controls and HUD

The in-window HUD reports mode, camera radius in `r_s`, FOV, disk radii,
half-thickness and inclination, integration tolerances, compute resolution,
frame time/FPS, and unresolved/invalid counts. It also exposes all rendering
toggles.

| Input | Action |
| --- | --- |
| Left- or middle-drag | Orbit the centered camera |
| Mouse wheel | Zoom; clamped to `r > r_s` because no static observer exists at or inside the horizon |
| `M` | Physical/Cinematic mode |
| `D` | Disk visibility |
| `B` | Procedural/uniform-dark background |
| `G` | Flamm's paraboloid overlay |
| `V` | Validation failure colors |
| `R` | Reverse disk rotation |
| `I` / `Shift+I` | Increase/decrease disk inclination |
| `T` / `Shift+T` | Increase/decrease disk half-thickness over `0.02-0.75 r_s` |
| `Home` | Reset the known camera view |

The optional grid is labelled **Flamm's paraboloid**: an embedding of a
constant-time equatorial spatial slice. It is not a literal view of
four-dimensional curvature.

Interactive rendering normally samples at half the framebuffer dimensions and
halves that again while the camera is moving. This affects image sampling only;
it does not change geodesic tolerances, equations, budgets, or classification.
Select “Full-resolution still” in the HUD or pass `--full-resolution` to use the
entire framebuffer.

## Validation and diagnostic commands

Run these from the executable directory:

```powershell
.\SchwarzschildValidation.exe
.\BlackHole3D.exe --validation-export
.\GpuValidationCompare.exe gpu_validation.csv
.\BlackHole3D.exe --thickness-validation-export
.\GpuValidationCompare.exe --thickness gpu_thickness_thin.csv gpu_thickness_thick.csv
.\BlackHole3D.exe --temperature-validation-export
.\GpuValidationCompare.exe --temperature gpu_temperature_validation.csv
.\BlackHole3D.exe --interaction-smoke
.\BlackHole3D.exe --performance-smoke
```

The thickness export compares per-pixel GPU classifications at half-thicknesses
`0.02 r_s` and `0.75 r_s`. It therefore fails if both inputs are silently
promoted to the same slab, as happened with the former `1.0 r_s` shader floor.
The temperature export runs the production shader's local-emission calculation
at five fixed radii and compares it with the CPU thin-disk implementation for
the default and tenfold accretion rates.

Additional reproducible captures are available:

```powershell
.\BlackHole3D.exe --capture-physical
.\BlackHole3D.exe --capture-cinematic
.\BlackHole3D.exe --capture-vacuum
.\BlackHole3D.exe --capture-grid
```

These write ignored BMP/CSV artifacts in the current build directory. The
vacuum capture uses Physical mode, hides the disk, and uses a uniform dark
background; it must contain no emissive ring.

## Architecture

| Owner | Responsibility |
| --- | --- |
| `physics/simulation_config.*` | Units, defaults, mode, camera, disk, integration and resolution configuration |
| `physics/schwarzschild.*` | Dependency-free CPU reference geodesics and classification |
| `physics/accretion_disk.*` | Approximate disk flux, temperature, redshift, and bolometric factor |
| `app/camera.*` | Static-observer camera and input state transitions |
| `rendering/shader_loader.*` | Checked shader loading, compilation, and program linking |
| `black_hole.cpp` | OpenGL renderer, GPU resource ownership, UI, captures, and runtime diagnostics |
| `geodesic.comp` | Dimensionless production GPU geodesics and radiance approximation |
| `tests/*` | CPU physics and CPU/GPU validation executables |

OpenGL resources and the GLFW window are released by the renderer owner before
context teardown. All project targets compile with `/W4 /permissive-` on MSVC
or `-Wall -Wextra -Wpedantic` elsewhere.

## Troubleshooting

- **Failed to create GLFW window:** update the graphics driver and verify
  OpenGL 4.3 support.
- **Failed to open a shader:** launch from the CMake configuration output
  directory (`build\Debug` or `build\Release`).
- **Integrated GPU selected:** add the exact `BlackHole3D.exe` path under
  Windows Settings → System → Display → Graphics and select High performance.
- **Yellow or magenta validation pixels:** inspect unresolved/invalid counts,
  then run the CPU/GPU validation export. These pixels are deliberately never
  classified as horizon capture.

No redistribution license has been chosen yet; see
[LICENSE_STATUS.md](LICENSE_STATUS.md).
