# M1 verification handoff

M1 is **open**. Windows editor automation has run in normal and VM-SIMD-disabled configurations; acceptance still requires Linux x64 runs and matching cross-host SSE2 native-noise captures from the same revisions. Native scalar noise is outside this milestone; the production VM scalar path remains supported.

## Revisions and environment

The FastNoise2 source revision is `c35d6041ce7efc05bc84e26d762f6a38d2dcc71e` and its FastSIMD revision is `f7e9ce6fca475731c0a3c115ec29dcfd36886b2a`. Record the final VoxelLand commit and FastNoise revision on **both** hosts with `git rev-parse HEAD` inside each submodule. Check the working trees and ensure both hosts use the same source and matching vendored source and build configuration. Record the exact UE 5.7 build, target configuration, compiler, compile-time `VOXELLAND_ENABLE_VM_SIMD` setting, requested and actual VM and native backends, and Automation result.

| Host / configuration | UE build | VoxelLand commit | FastNoise plugin commit and source revisions | VM / native backend | Result |
| --- | --- | --- | --- | --- | --- |
| Win64, normal, Development Editor / NullRHI | 5.7 CL 51494982 | `8799a0c2b890522c5c8c9cce05304646b15dd7ab` | `0be510f3932dfa6b48927a342c3876f4232a2798`; vendored revisions above | automatic VM SIMD4 / native SSE2 | M1 13/13; same-host comparison against VM-SIMD-disabled capture passed |
| Linux x64, normal | pending | pending | pending | pending | pending |
| Win64, VM SIMD compiled out, Development Editor / NullRHI | 5.7 CL 51494982 | `8799a0c2b890522c5c8c9cce05304646b15dd7ab` | `0be510f3932dfa6b48927a342c3876f4232a2798`; vendored revisions above | automatic VM scalar / native SSE2 | M1 13/13, including scalar fallback and forced SIMD rejection |
| Linux x64, VM SIMD compiled out and native SSE2 | pending | pending | pending | pending | pending |

FastNoise2 and FastSIMD compile from the plugin's vendored source through UnrealBuildTool. No separate native library or DLL is staged. This integration pins SSE2 on Win64 and Linux x64. Migrating from the old native version may change noise output; do not reuse old goldens without verification.

## Run

Build the host project and plugin for the selected UE 5.7 configuration, then use the corresponding editor commandlet executable. Substitute paths appropriate to each machine:

```powershell
& '<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' '<project>/VoxelLandExample.uproject' -unattended -NullRHI -nosplash '-ExecCmds=Automation RunTests VoxelLand.M1;Quit' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=<reports>/win64'
```

```sh
<UE>/Engine/Binaries/Linux/UnrealEditor <project>/VoxelLandExample.uproject -unattended -NullRHI -nosplash '-ExecCmds=Automation RunTests VoxelLand.M1;Quit' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=<reports>/linux'
```

Repeat with VM SIMD compiled out. Record actual backend selections and all test results; investigate every mismatch instead of refreshing golden values to make the test pass.

To explicitly compile the VM SIMD kernels out, temporarily add this line inside the `VoxelLandAlgo` constructor in `Source/VoxelLandAlgo/VoxelLandAlgo.Build.cs`, then rebuild that module and its host for the intended platform:

```csharp
PrivateDefinitions.Add("VOXELLAND_ENABLE_VM_SIMD=0");
```

Run the full suite above with the additional command-line argument `-VoxelLandM1ExpectScalarVM`. The `VoxelLand.M1.VMFallback` test then requires automatic VM selection to be scalar and a forced SIMD request to return `BackendUnavailable`; the remaining tests still exercise scalar batches. The flag checks the build and does not itself disable SIMD. Record this build configuration separately, then remove the temporary definition and rebuild before returning to normal SIMD-enabled builds. Compile-out affects the VM only; native noise remains SSE2.

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

The checked-in plane cases use exact integer lattice recipes for expected density, then independently serialize little-endian `int16` bytes and hash them with UE's XXH3 implementation. They check two voxel sizes, positive and negative keys, all three padded overlap planes on each axis, and report the first different lattice coordinate and values. Other fixtures cover representative analytic shapes, quantization, invalid programs/profiles/LOD, worker-only execution, VM scalar/automatic parity on noise at sample counts 1, 2, 3 and 256, explicit native-scalar rejection, stable seed-slot behavior, and concurrent workers sharing a generator snapshot.

A Windows compile-out capture was compared successfully with a normal Windows build on the same host. It is not a cross-host comparison; no shared Windows/Linux noise golden table has been established. Use the capture and compare procedure above on Linux, investigate any difference, and add an agreed fixed numeric table only after both hosts agree. Linux SIMD compile-out and cross-platform acceptance remain open.

## Windows VM-SIMD compile-out evidence

On Windows 11 x64 with a Ryzen 9 9950X3D, UE 5.7 CL 51494982 Development Editor and MSVC 14.51.36243, a temporary `PrivateDefinitions.Add("VOXELLAND_ENABLE_VM_SIMD=0")` build succeeded. `VoxelLand.M1` ran with `-VoxelLandM1ExpectScalarVM` and passed 13/13; `VMFallback` required automatic scalar selection and `BackendUnavailable` for forced SIMD. The M1 cross-host capture test in that full run warned that no comparison path was supplied. A separate capture recorded `platform=WindowsEditor VM=1 native=2` (scalar VM, native SSE2), and the capture contains 16 cases. `VoxelLand.M3` passed 29/29 in the same compile-out configuration, with expected warnings that forced VM SIMD parity and the SIMD fallback path were not exercised.

The original `VoxelLandAlgo.Build.cs` bytes were restored and the normal configuration rebuilt successfully. A restored normal-build `VoxelLand.M1` run passed 13/13, reported `VM=2 native=2` (SIMD4 VM, native SSE2), and matched all captured noise arrays and hashes against the compile-out build on this same Windows host. This does not verify Linux or cross-host parity. One initial restore rebuild was blocked by Rider's concurrent project-file generation holding the build-rules DLL; the retry succeeded. Logs in the host project's `Saved/Logs` are `M1_VMSimdDisabled_Build.txt`, `M1_VMSimdDisabled_Automation.log`, `M3_VMSimdDisabled_Automation.log`, `M1_VMSimdDisabled_Capture.log`, `M1_VMSimdRestored_RetryBuild.txt` and `M1_VMRestored_FullSameHostCompare.log`. The capture is `Saved/M1_VMSimdDisabled_Capture.txt`.
