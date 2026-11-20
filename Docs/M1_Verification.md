# M1 verification handoff

M1 is **open**. The source fixtures are authored but have not been built or executed. Acceptance requires successful runs on both Win64 and Linux x64, matching native-noise goldens from the same revisions, and a full scalar-native run. The stock x64 FastNoise2 dependency excludes its scalar backend; `VoxelLand.M1.NativeScalarRequired` deliberately fails until a matching scalar-enabled native build is staged. VM scalar mode alone does not satisfy that gate.

## Revisions and environment

The FastNoise submodule inspected during implementation was `cf7709f205ed25f002781d88e080985e39df3ba9`. Record the final VoxelLand commit and FastNoise revision on **both** hosts with `git rev-parse HEAD` inside each submodule. Check the working trees and ensure both hosts use the same source and matching native headers and library. Record the exact UE 5.7 build, target configuration, compiler, compile-time `VOXELLAND_ENABLE_VM_SIMD` setting, requested and actual VM and native backends, and Automation result.

| Host / configuration | UE build | VoxelLand commit | FastNoise commit and variant | VM / native backend | Result |
| --- | --- | --- | --- | --- | --- |
| Win64, normal | pending | pending | pending | pending | pending |
| Linux x64, normal | pending | pending | pending | pending | pending |
| Win64, VM SIMD compiled out and native scalar enabled | pending | pending | pending | pending | pending |
| Linux x64, VM SIMD compiled out and native scalar enabled | pending | pending | pending | pending | pending |

The native library, its compile configuration, and its headers must come from the same FastNoise revision. The bundled headers disable scalar on x64. Build and stage a scalar-enabled matching FastNoise2 variant on each host; do not substitute only the DLL/shared object while retaining headers that describe a different backend configuration. Preserve the stock dependency for comparison. Verify the native module and library load before scheduling work. Library existence alone is not a successful load or a parity result.

## Run

Build the host project and plugin for the selected UE 5.7 configuration, then use the corresponding editor commandlet executable. Substitute paths appropriate to each machine:

```powershell
& '<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<project>/VoxelLandExample.uproject' -unattended -NullRHI -nosplash '-ExecCmds=Automation RunTests VoxelLand.M1;Quit' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=<reports>/win64'
```

```sh
<UE>/Engine/Binaries/Linux/UnrealEditor <project>/VoxelLandExample.uproject -unattended -NullRHI -nosplash '-ExecCmds=Automation RunTests VoxelLand.M1;Quit' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=<reports>/linux'
```

Repeat with the scalar-enabled native dependency and VM SIMD compiled out. The `NativeScalarRequired` test is an explicit acceptance gate; when using the stock x64 dependency, its backend-unavailable failure is expected evidence that this gate is still pending, not a passing result. Record actual backend selections and all test results; investigate every mismatch instead of refreshing golden values to make the test pass.

To explicitly compile the VM SIMD kernels out, temporarily add this line inside the `VoxelLandAlgo` constructor in `Source/VoxelLandAlgo/VoxelLandAlgo.Build.cs`, then rebuild that module and its host for the intended platform:

```csharp
PrivateDefinitions.Add("VOXELLAND_ENABLE_VM_SIMD=0");
```

Run the full suite above with the additional command-line argument `-VoxelLandM1ExpectScalarVM`. The `VoxelLand.M1.VMFallback` test then requires automatic VM selection to be scalar and a forced SIMD request to return `BackendUnavailable`; the remaining tests still exercise scalar batches. The flag checks the build and does not itself disable SIMD. Record this build configuration separately, then remove the temporary definition and rebuild before returning to normal SIMD-enabled builds. Compile-out of the VM does not enable FastNoise's independent native scalar backend.

## Capture and compare cross-host noise arrays

On the first host, execute the capture test and choose an output path in a writable directory:

```powershell
& '<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<project>/VoxelLandExample.uproject' -unattended -NullRHI -nosplash '-ExecCmds=Automation RunTests VoxelLand.M1.CrossHostCapture;Quit' '-TestExit=Automation Test Queue Empty' '-VoxelLandM1Capture=<reports>/voxel-m1-win64.txt'
```

Transfer that file unchanged to the second host and run the comparison against freshly generated samples:

```sh
<UE>/Engine/Binaries/Linux/UnrealEditor <project>/VoxelLandExample.uproject -unattended -NullRHI -nosplash '-ExecCmds=Automation RunTests VoxelLand.M1.CrossHostCapture;Quit' '-TestExit=Automation Test Queue Empty' '-VoxelLandM1Compare=<reports>/voxel-m1-win64.txt' '-VoxelLandM1Capture=<reports>/voxel-m1-linux.txt'
```

The test generates Perlin and fBm in 2D and 3D, at both voxel sizes and two keys/seeds, then compares the entire X-fastest quantized arrays and hashes. The first differing sample is reported with program opcode, seed, profile, chunk key, expected and actual hashes, lattice coordinate, and values. A matching hash without matching bytes is a failure. Captures use a version-specific text format and contain no observed numbers until generated by an actual host. Check source revisions and backend selection in the Automation log before treating a comparison as evidence. Archive the reports and captures with the run record.

## Current fixture coverage and remaining acceptance work

The checked-in plane cases use exact integer lattice recipes for expected density, then independently serialize little-endian `int16` bytes and hash them with UE's XXH3 implementation. They check two voxel sizes, positive and negative keys, all three padded overlap planes on each axis, and report the first different lattice coordinate and values. Other fixtures cover representative analytic shapes, quantization, invalid programs/profiles/LOD, worker-only execution, VM scalar/automatic parity on noise at sample counts 1, 2, 3 and 256, native scalar/SSE2 comparisons, stable seed-slot behavior, and concurrent workers sharing a generator snapshot.

The native-noise outputs have **not** been captured or compared between hosts; there is no observed shared noise golden table yet. Use the capture and compare procedure above, investigate any difference, and add an agreed fixed numeric table only after both hosts agree. The SIMD compile-out and fallback assertions are authored but have not been built or run. M2 readiness cannot be certified until the complete plan's verification matrix and cross-platform goldens pass.
