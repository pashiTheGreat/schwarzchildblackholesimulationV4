# Source status

The active application is `BlackHole3D`, built from `black_hole.cpp` and
`geodesic.comp`.  CPU validation has one authoritative owner in
`physics/schwarzschild.cpp`; it has no OpenGL or GLFW dependency.

`BlackHole2D` is retained as a separate, historical 2D visualization target.
It is not a validation oracle and does not share its old integrator with the
production reference library.

The following archival experiments were removed from the active tree. They
remain recoverable from Git history:

- `CPU-geodesic.cpp` — an older CPU/OpenGL prototype with an independent
  meter-scale integrator.
- `ray_tracing.cpp` — an early generic ray-tracing experiment.
- `black_hole.exe` — a legacy generated binary.
- `Gravity_Sim.zip` — a nested historical source archive.
- `vs_code/` — editor configuration for obsolete MinGW/CUDA paths and targets.

No archival artifact is a build input or may be cited as current physics.
