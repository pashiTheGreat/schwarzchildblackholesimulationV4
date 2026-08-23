# Clean final validation report

Date: 2026-08-23 (Asia/Calcutta)

## Outcome

**Overall: PASS.** The disk half-thickness and temperature fixes pass a fresh
Debug/Release build, CPU validation, independent CPU/GPU comparisons,
interaction and resizing checks, performance smoke, and visual capture audit.

The strongest accurate project description is:

> **Validated Schwarzschild null-geodesic renderer with an approximate
> finite-thickness, zero-torque accretion-disk emission model and explicitly
> separate Physical and Cinematic presentation modes.**

This does not claim a complete physically accurate black-hole image. The
vacuum Schwarzschild geodesics, capture/escape classification, shadow angle,
disk intersection response, and shader/CPU disk temperatures are validated
within the tolerances below. Disk plasma and radiative transfer remain
approximations.

## Fresh environment and configuration

The audit used a new directory, `build_audit_20260823`. An incomplete directory
from an initially sandbox-blocked vcpkg attempt was removed before the
successful configure, so the recorded build began without a CMake cache or
previous build products.

- Generator: Visual Studio 18 2026
- Compiler: MSVC 19.50.35728.0, C++17
- Windows SDK: 10.0.26100.0
- Architecture/triplet: x64 / `x64-windows`
- Toolchain: `C:/vcpkg/scripts/buildsystems/vcpkg.cmake`
- vcpkg: all nine manifest packages restored from the binary cache
- Direct dependencies: GLEW 2.3.1, GLFW 3.5.1, GLM 1.0.3, ImGui 1.92.8
- OpenGL vendor: NVIDIA Corporation
- GPU: NVIDIA GeForce RTX 3050 Ti Laptop GPU
- OpenGL: 4.3.0 NVIDIA 595.97

Exact successful configure command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  -S . -B build_audit_20260823 -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

Result: configuration and generation completed successfully; build files were
written to `build_audit_20260823`.

## Builds and warnings

Exact commands:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --build build_audit_20260823 --config Debug --target `
  BlackHole3D BlackHole2D SchwarzschildValidation GpuValidationCompare

& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --build build_audit_20260823 --config Release --target `
  BlackHole3D BlackHole2D SchwarzschildValidation GpuValidationCompare
```

Result: **PASS** in Debug and Release. All four targets linked successfully.
MSVC `/W4 /permissive-` produced no project-owned warnings.

## CTest and CPU reference

Exact commands:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' `
  --test-dir build_audit_20260823 -C Debug --output-on-failure

& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' `
  --test-dir build_audit_20260823 -C Release --output-on-failure
```

Results:

```text
Debug:   1/1 passed; SchwarzschildValidation 0.15 s; total 0.16 s
Release: 1/1 passed; SchwarzschildValidation 0.07 s; total 0.08 s
```

Selected clean-run CPU results:

| Check | Result |
| --- | ---: |
| Static-tetrad maximum null residual | `6.66133814775e-16` |
| Maximum conserved-energy drift | `3.77475828373e-15` |
| Maximum `L^2` drift | `5.6132876125e-13` |
| Photon sphere | `1.5 r_s` exactly in the test |
| Critical impact parameter | `2.59807621135 r_s` |
| Weak-field deflection at `b=100 r_s` | `0.0202999649771 rad` |
| Analytical default-camera shadow half-angle | `27.7071984152 deg` |
| RK4 step-halving error ratio | `15.9823870877` |
| Default CPU disk temperature at `4.5 r_s` | `5866.05093806 K` |
| `T(10 dotM)/T(dotM)` | `1.77827941004` |

The default-configuration test also reported
`disk_half_thickness=0.12 r_s` with valid control range `[0.02,0.75] r_s`.

## Standard GPU/CPU validation and shadow

Commands, run from `build_audit_20260823\Release`:

```powershell
.\BlackHole3D.exe --validation-export
.\GpuValidationCompare.exe gpu_validation.csv
```

Exact numerical result:

```text
pixels=3072 mismatches=2 allowed=8
unresolved_or_invalid=0
max_initial_null=6.00973e-07
energy_span=0
max_escaped_final_null=2.97641e-06
shadow_edge_degrees=27.7049 analytical_degrees=27.7072
sampling_half_width_degrees=3.18055e-15 edge=PASS
overall=PASS
[OPENGL] debug_errors=0
```

The measured vacuum shadow differs from the analytical result by `0.0023 deg`.
The two GPU/CPU classifications that differ lie at the discretized shadow
boundary and remain below the explicit allowance of eight. No failed ray was
classified as capture.

## Disk half-thickness verification

Commands:

```powershell
.\BlackHole3D.exe --thickness-validation-export
.\GpuValidationCompare.exe --thickness gpu_thickness_thin.csv gpu_thickness_thick.csv
```

Exact result:

```text
resolution=160x120
thin=0.02_r_s disk_hits=5726
thick=0.75_r_s disk_hits=10678
changed_status_pixels=4952
minimum_changed_pixels=960
unresolved_or_invalid=0
overall=PASS
[OPENGL] debug_errors=0
```

The 4,952 changed classifications prove that disk thickness changes GPU
intersection geometry. The default `0.12 r_s` half-thickness is independently
present in the CPU configuration report and all four capture HUDs; it is not
promoted to a `1 r_s` slab.

## Disk temperature CPU/GPU agreement

Commands:

```powershell
.\BlackHole3D.exe --temperature-validation-export
.\GpuValidationCompare.exe --temperature gpu_temperature_validation.csv
```

Exact clean-run samples:

| Radius | GPU default | CPU default | GPU at `10 dotM` | GPU scaling |
| ---: | ---: | ---: | ---: | ---: |
| `3.5 r_s` | `5647.60986 K` | `5647.61006 K` | `10043.0283 K` | `1.77827940` |
| `4.5 r_s` | `5866.05127 K` | `5866.05094 K` | `10431.4775 K` | `1.77827929` |
| `6 r_s` | `5313.83496 K` | `5313.83502 K` | `9449.48242 K` | `1.77827924` |
| `9 r_s` | `4296.92188 K` | `4296.92172 K` | `7641.12744 K` | `1.77827935` |
| `12 r_s` | `3611.60938 K` | `3611.60912 K` | `6422.45020 K` | `1.77827930` |

```text
temperature_relative_tolerance=2e-05
temperature_absolute_tolerance_K=0.05
max_temperature_relative_error=7.0626114e-08
max_rotation_temperature_delta_K=0
r4.5_shift_l_positive=1.10368633
shift_l_negative=0.778389275
reversed_shift_l_positive=0.778389275
reversed_shift_l_negative=1.10368633
doppler_reversal=PASS
overall=PASS
[OPENGL] debug_errors=0
```

The Physical disk temperature at `4.5 r_s` is approximately `5,866 K` and
agrees with `physics/accretion_disk.cpp`. Reversing rotation swaps Doppler
asymmetry while leaving local emitted temperature unchanged.

## Interaction and resize smoke

Command:

```powershell
.\BlackHole3D.exe --interaction-smoke
```

Exact result:

```text
thickness_geometry=PASS thin_disk_hits=22964 thick_disk_hits=42774 changed_pixels=19810
motion_model_stability=PASS
800x600 compute=400x300 unresolved=0 invalid=0 interactive PASS
1280x720 compute=1280x720 unresolved=0 invalid=0 full PASS
600x900 compute=300x450 unresolved=0 invalid=0 interactive PASS
overall=PASS
[OPENGL] debug_errors=0
```

The T/Shift+T path therefore changes production GPU geometry and returns to the
original thin classification map. All three aspect ratios and sampling modes
produced exactly one classification per compute pixel.

## Performance smoke

Command:

```powershell
.\BlackHole3D.exe --performance-smoke
```

Exact result:

```text
allocation_path=optimized
mode=Physical-vacuum
resolution=320x240
abs_tol=2e-05 rel_tol=2e-05
average_compute_ms=13.560 samples=30
captured=37450 escaped=39350 disk=0 reserved=0
unresolved=0 invalid=0 total=76800
[OPENGL] debug_errors=0
```

No comparison with the legacy allocation path is claimed in this clean report
because the requested clean run executed only the current optimized path.

## Captures and visual inspection

Commands:

```powershell
.\BlackHole3D.exe --capture-physical
.\BlackHole3D.exe --capture-cinematic
.\BlackHole3D.exe --capture-vacuum
.\BlackHole3D.exe --capture-grid
```

Each command reported `status_total=76800` and `[OPENGL] debug_errors=0`.
The generated files were all 800x600, 24-bit BMPs of 1,440,054 bytes:

- `physical.bmp`
- `cinematic.bmp`
- `physical_vacuum.bmp`
- `flamm_paraboloid.bmp`

Visual audit at original resolution:

- All captures are upright, correctly proportioned, and free of unintended
  stretching or framebuffer clipping.
- Physical and Cinematic use identical disk/shadow geometry. Physical retains
  neutral blackbody-based coloring; Cinematic retains its explicit orange
  grading and artistic enhancement.
- The default `0.120 r_s` disk produces bounded direct and lensed bands. It
  does not cover the view like the former effective `1 r_s` foreground slab.
- The vacuum capture has no visible emissive photon ring. A pixel audit outside
  the HUD rectangle found `vacuum_max_channel_outside_hud=0` and
  `vacuum_nonblack_pixels_outside_hud=0`. Source inspection also confirmed that
  `photonRingEmission` is added only when `renderMode != Physical`.
- The Flamm-paraboloid wireframe is identifiable and correctly oriented. Some
  geometry naturally exits the upper/right viewport; there is no inversion or
  image corruption.
- The HUD is fully readable and unclipped and reports zero unresolved/invalid
  rays. It intentionally occupies a substantial part of the upper-left
  quadrant and therefore obscures some scene content; this is a remaining
  capture-layout limitation, not a physics or rendering failure.

## OpenGL and repository audit

- Every clean-run GPU command reported `[OPENGL] debug_errors=0`.
- Standard GPU validation, performance, interaction, and capture HUDs reported
  zero unresolved/invalid rays.
- `git diff --check` passed.
- Build and generated validation/capture artifacts remain ignored; this audit
  changed no implementation source. Only this report was updated during the
  final audit.

## Remaining scientific approximations

- Spacetime is Schwarzschild only: no Kerr spin, frame dragging, charge, or
  dynamical metric.
- The accretion disk is an approximate finite-thickness, optically thick,
  zero-torque emitter; it is not an MHD/plasma simulation and is not a faithful
  model of quiescent Sagittarius A*.
- RGB blackbody mapping is not a sampled spectrum. Bolometric `g^4` weighting
  is not frequency-dependent radiative transfer; absorption, scattering,
  polarization, and camera-band response are absent.
- Production GPU integration is IEEE-754 single precision. The CPU reference
  uses double precision; finite raster resolution and the `1.0005 r_s` capture
  surface limit shadow-edge precision.
- Procedural density and Cinematic photon-ring/high-order enhancements are
  presentation devices. They do not alter the validated geodesic equations.
- The optional Flamm paraboloid is an embedding diagram of an equatorial
  spatial slice, not a literal rendering of four-dimensional curvature.
- No external vendor object-leak or GPU-memory profiler was run. Resource
  lifetime confidence comes from code ownership and repeated clean exits.

No remaining limitation is a correctness blocker for the stated validated
Schwarzschild-geodesic and approximate-disk scope.
