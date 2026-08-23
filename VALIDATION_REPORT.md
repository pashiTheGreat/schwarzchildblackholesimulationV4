# Final validation report

Date: 2026-08-23 (Asia/Calcutta)

## Outcome

All core validation gates pass. The accurate project description is:

> **Validated Schwarzschild geodesic renderer with an approximate accretion-disk model.**

The report does not claim a complete “physics accurate” black-hole image. The
vacuum geodesics, capture/escape boundary, invariants, and shadow angle are
validated; disk emission and RGB presentation remain documented
approximations.

## Clean-room environment

- Fresh build directory: `build_final` (did not exist before configuration)
- Generator: Visual Studio 18 2026
- Compiler: MSVC 19.50.35728.0, C++17
- Windows SDK: 10.0.26100.0
- Architecture/triplet: x64 / `x64-windows`
- CMake: bundled with Visual Studio 18
- Dependencies restored by vcpkg manifest:
  - GLEW 2.3.1
  - GLFW 3.5.1
  - GLM 1.0.3
  - ImGui 1.92.8 with GLFW and OpenGL 3 bindings
- OpenGL vendor: NVIDIA Corporation
- GPU: NVIDIA GeForce RTX 3050 Ti Laptop GPU
- OpenGL: 4.3.0 NVIDIA 595.97

The clean configure completed from the manifest with:

```powershell
cmake -S . -B build_final -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

## Builds and compiler diagnostics

The following commands built `BlackHole3D`, `BlackHole2D`,
`SchwarzschildValidation`, and `GpuValidationCompare` in both configurations:

```powershell
cmake --build build_final --config Debug --target `
  BlackHole3D BlackHole2D SchwarzschildValidation GpuValidationCompare
cmake --build build_final --config Release --target `
  BlackHole3D BlackHole2D SchwarzschildValidation GpuValidationCompare
```

Result: **PASS**. MSVC `/W4 /permissive-` produced no project-owned warnings in
the final builds. Every shader compile and link path checks and reports the
driver log; all runtime shader/program creation succeeded.

## CPU validation

Commands:

```powershell
ctest --test-dir build_final -C Debug --output-on-failure
ctest --test-dir build_final -C Release --output-on-failure
```

Results:

- Debug: 1/1 passed in 0.14 s.
- Release: 1/1 passed in 0.08 s.
- Initial tetrad: maximum null residual `6.66e-16` over 665 directions.
- Invariants: maximum `dE=3.77e-15`, `dL²=5.61e-13`, and null residual
  `7.72e-15` under the stated `2e-10` test bound.
- Photon sphere: `r=1.5 r_s`; critical impact parameter
  `2.59807621135 r_s`.
- Weak-field deflection at `b=100 r_s`: `0.020299965 rad` versus leading
  `0.02 rad` (1.50% difference, under 2.5%).
- Analytical default-camera shadow half-angle: `27.7071984°`.
- RK4 step-halving error ratio: `15.9824`, consistent with fourth order.
- Mass-scale and adaptive classification convergence: pass.
- Thin-disk temperature/accretion scaling and rotation-reversal redshift: pass.

Full test definitions and tolerances are in
[PHYSICS_VALIDATION.md](PHYSICS_VALIDATION.md) and
[`tests/schwarzschild_validation.cpp`](tests/schwarzschild_validation.cpp).

## GPU/CPU agreement and raster shadow

Commands, run from `build_final\Release`:

```powershell
.\BlackHole3D.exe --validation-export
.\GpuValidationCompare.exe gpu_validation.csv
```

Result: **PASS**.

| Measurement | Result |
| --- | ---: |
| Deterministic rays | 3,072 (64×48) |
| GPU/CPU boundary classifications | 2 differences; allowance 8 |
| Unresolved or invalid | 0 |
| Maximum initial GPU null residual | `6.00973e-7` |
| GPU energy span | `0` |
| Maximum escaped final null residual | `2.97641e-6` |
| Measured raster shadow half-angle | `27.7049°` |
| Analytical shadow half-angle | `27.7072°` |
| Shadow edge | PASS |

The two classification differences occur at the discretized critical edge and
remain below the explicit eight-pixel allowance. Failed rays do not contribute
to the shadow.

## Controls, resizing, and diagnostics

Command:

```powershell
.\BlackHole3D.exe --interaction-smoke
```

Result: **PASS** with zero OpenGL debug errors.

- All mode, disk, background, grid, validation, rotation, inclination,
  thickness, and reset state transitions passed.
- Attempted inward zoom clamped at `1.05 r_s`, outside the horizon.
- Camera-motion model stability passed at fixed sampling resolution.
- 800×600 framebuffer → 400×300 interactive compute: pass.
- 1280×720 framebuffer → 1280×720 full-resolution compute: pass.
- 600×900 framebuffer → 300×450 interactive compute: pass.
- Each resize preserved aspect ratio, produced exactly one status per compute
  pixel, and reported zero unresolved/invalid rays.

## Visual captures

Commands:

```powershell
.\BlackHole3D.exe --capture-physical
.\BlackHole3D.exe --capture-cinematic
.\BlackHole3D.exe --capture-vacuum
.\BlackHole3D.exe --capture-grid
```

All four 800×600 captures completed with `status_total=76800` for their 320×240
compute images and `debug_errors=0`. They are generated, ignored artifacts in
`build_final\Release` so binary screenshots do not pollute source control:

- `physical.bmp`: readable HUD; neutral blackbody-based Physical disk.
- `cinematic.bmp`: identical geometry with visible orange grading and artistic
  enhancement.
- `physical_vacuum.bmp`: uniform dark scene with no artificial glowing photon
  ring.
- `flamm_paraboloid.bmp`: visible post-image overlay labelled exactly
  “Flamm's paraboloid.”

The captures were visually inspected at original 800×600 resolution. No image
was inverted or stretched.

## Fixed-view performance

Benchmark command:

```powershell
.\BlackHole3D.exe --performance-smoke --legacy-allocations
.\BlackHole3D.exe --performance-smoke
```

OpenGL `GL_TIME_ELAPSED` measured 30 samples after five warm-up frames. Both
paths used Release, Physical-vacuum mode, a 320×240 compute image, absolute and
relative tolerances `2e-5`, the same camera, GPU, termination rules, and 76,800
classified pixels.

| Path | Maximum adaptive step | Mean GPU compute |
| --- | ---: | ---: |
| Baseline (legacy diagnostic allocation/write path) | `0.05` | `24.634 ms` |
| Final (persistent minimal diagnostics; validated bound) | `0.10` | `12.897 ms` |

Improvement: **47.6%**. Both runs classified exactly 37,450 captured and 39,350
escaped rays with zero disk hits, unresolved, or invalid statuses. The final
GPU export then passed CPU classification, invariant, and analytical shadow
checks, so the larger error-controlled step bound did not change validated
results.

Output textures resize only when dimensions change; grid buffers are static;
full per-pixel invariant buffers are allocated only for validation. Motion
changes sampling resolution only, not equations, tolerances, or termination.

Schwarzschild orbital-plane reduction was investigated but not implemented.
It could reduce the state further, but replacing the already validated 3D
mapping would require new plane/camera/disk equivalence tests.

## Error and lifetime audit

- Every final GPU command reported `[OPENGL] debug_errors=0`.
- Validation reported no NaN/invalid rays.
- Renderer teardown deletes all owned UBOs, SSBOs, VBOs, EBOs, VAOs, texture,
  and programs before destroying the GLFW context; repeated smoke/capture runs
  exited normally.
- No dedicated external OpenGL object-leak profiler was available. Resource
  lifetime was checked by ownership/code audit and repeated clean exits, not by
  a vendor memory-leak tool.
- `git diff --check` is part of the final repository audit.

## Known limitations

- The spacetime is Schwarzschild only: no Kerr spin/frame dragging or charge.
- GPU integration is single precision; the independent CPU reference uses
  double precision.
- The disk is an approximate luminous thin emitter and is not representative
  of quiescent Sagittarius A*'s full plasma state.
- RGB blackbody mapping and bolometric `g^4` are not a frequency-resolved
  radiative-transfer calculation.
- Finite resolution and a capture surface at `1.0005 r_s` limit raster-edge
  precision.
- Near-separatrix rays remain numerically sensitive, but unresolved/invalid
  states remain visible and separate from capture.
- Project redistribution terms remain blocked on choosing a license; see
  [LICENSE_STATUS.md](LICENSE_STATUS.md).

No remaining limitation is a correctness blocker for the stated validated
Schwarzschild-geodesic scope.
