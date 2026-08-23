# Physics and validation

## Scope and coordinates

The renderer assumes the Schwarzschild metric for a nonrotating, uncharged,
isolated mass:

```text
ds² = -f dt² + f⁻¹ dr² + r²(dθ² + sin²θ dφ²),  f = 1 - r_s/r.
```

Geodesics use dimensionless units with `r_s = 1` and
`lambda_dimensionless = lambda_SI / r_s`. SI mass and radius are retained for
labels and disk thermodynamics. Coordinates are Y-up:

```text
x = r sin(theta) cos(phi)
y = r cos(theta)
z = r sin(theta) sin(phi)
```

The CPU reference is `physics/schwarzschild.cpp`; the matching production path
is `geodesic.comp`. The renderer does not use the approximate disk emissivity to
alter a geodesic.

## Static-observer photon initialization

A normalized screen direction is resolved into the local orthonormal spherical
basis as `(n_r, n_theta, n_phi)`. For arbitrary positive local energy:

```text
k^t     = E_local / sqrt(f)
k^r     = E_local sqrt(f) n_r
k^theta = E_local n_theta / r
k^phi   = E_local n_phi / (r sin(theta))
E_inf   = f k^t
```

This makes the screen direction a local measurement rather than a flat-space
coordinate derivative. The camera is clamped to at least `1.05 r_s`; a static
worldline is impossible at or inside the event horizon.

## Integration and termination

Both reference and GPU implementations use the Schwarzschild coordinate
geodesic equations. The production radial acceleration is evaluated in the
constraint-reduced, cancellation-resistant form

```text
r'' = L² (r - 1.5) / r⁴,
```

which is algebraically equivalent for a null geodesic to the full coordinate
equation containing the required `r f` angular term. Total angular momentum
`L²`, not only its axial component, is used for general 3D rays.

The integrator is fourth-order Runge–Kutta with step doubling for local error
control. Production defaults are absolute/relative tolerances `2e-5`, minimum
step `1e-5`, maximum step `0.10`, maximum 24,000 accepted attempts, capture
radius `1.0005 r_s`, and escape radius at least `80 r_s`. Curvature and horizon
distance bound proposed steps. Horizon and disk crossings are interpolated.

Each ray terminates as captured, escaped, disk hit, unresolved, or invalid.
Unresolved/invalid rays have separate counters and conspicuous validation
colors; they are never painted as capture.

## Accretion-disk approximation

Physical mode uses a generic, optically thick, geometrically thin,
zero-torque emitter with inner edge at the Schwarzschild ISCO, `3 r_s`:

```text
F(r) = 3 G M dotM / (8 pi r³) (1 - sqrt(r_in/r))
T_eff = (F/sigma)^(1/4)
```

The shader maps effective temperature through an approximate blackbody RGB
conversion. It evaluates the frequency shift from four-vectors for a static
observer and circular Schwarzschild emitter,

```text
g = (-k.u_observer) / (-k.u_emitter),
```

then applies `g^4`, so the rendered quantity is explicitly a bolometric
intensity approximation. Procedural density variation modulates emissivity but
never ray motion. Physical mode has no fixed orange palette, artificial photon
sphere emission, or undocumented radiance boost.

This disk is not a faithful model of quiescent Sagittarius A*. That source is
usually described by a hot, optically thin, radiatively inefficient plasma;
the project does not solve plasma dynamics, absorption, polarization, or
frequency-dependent radiative transfer.

## Passing evidence

The automated source of record is
[`tests/schwarzschild_validation.cpp`](tests/schwarzschild_validation.cpp).
The latest verified CPU results include:

| Check | Measured result | Pass tolerance |
| --- | --- | --- |
| Tetrad null norm | max `6.66e-16` over 665 rays | `2e-13` absolute |
| Conserved quantities | max `dE=3.77e-15`, `dL²=5.61e-13`, null `7.72e-15` | `2e-10` absolute |
| Photon sphere | max `|r-1.5| = 0` for ten periods | `2e-10` absolute |
| Critical impact parameter | `2.59807621135 r_s` | `2e-10` absolute |
| Weak-field deflection, `b=100 r_s` | `0.020299965 rad` vs leading `0.02 rad` | 2.5% relative |
| Default shadow half-angle | `27.7071984°` at `r=4.9976 r_s` | `0.01°` absolute |
| RK4 convergence | error ratio `15.9824` when halving step | expected approximately 16 |
| Disk accretion scaling | `T(10 dotM)/T = 1.77827941` | `1e-12` relative |

The deterministic 64×48 GPU export is checked by
[`tests/gpu_validation_compare.cpp`](tests/gpu_validation_compare.cpp). On the
verified RTX 3050 Ti run it reported 3,072 rays, two classification differences
at the discretized shadow boundary (allowance eight), zero unresolved/invalid,
maximum initial null residual `6.01e-7`, zero energy span, and maximum escaped
final null residual `5.14e-6`.

`VALIDATION_REPORT.md` records the final clean-build commands, hardware,
screenshots, timing, and any blocked checks.

## Numerical and rendering limitations

- GPU integration uses 32-bit float; the double CPU suite is the accuracy
  reference.
- Finite pixel resolution and a capture surface just outside the coordinate
  horizon limit the measured raster edge.
- Schwarzschild spherical symmetry could reduce each ray to an orbital-plane
  system. This was investigated but deferred: the current full 3D mapping is
  already validated, while a reduced implementation would need new plane-to-
  camera and disk-intersection equivalence tests before replacing it.
- RGB blackbody mapping is not a sampled spectrum, and bolometric `g^4` is not
  a camera-band radiative-transfer calculation.
- The disk has finite procedural thickness and emissivity structure for
  visibility; neither represents a self-consistent plasma solution.
- Rays exactly on a separatrix are numerically sensitive. Such failures remain
  explicit rather than being merged into the black-hole silhouette.
