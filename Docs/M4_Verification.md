# M4 deterministic capture handoff

`VoxelLand.M4.Determinism.Capture` samples a 2D Perlin control program, evaluates a linear LUT and builds base heights for three seeds. It checks repeat control evaluation and compares LUT and height outputs against an independently computed integer reference. It separately exercises a fixed synthetic Q8 height field through the finite global erosion prepass and native 3D overhang density generation. It checks two adjacent chunks for three seeds. It compares serial requests with reverse-order requests queued to the thread pool, each with an independent worker. Scheduling can still run those requests on one thread; this is not a configurable worker-count stress test.

The version 2 capture contains little-endian FNV-1a hashes of the control samples, curve outputs, computed base heights and synthetic erosion input, plus the prepass grid identity and each chunk's existing density hash. The control/LUT check and synthetic erosion path are separate fixtures; it does not feed those control values into erosion or capture individual erosion stages. Authored graph compilation, curve baking and individual erosion stages still need cross-host fixtures before this establishes full M4 acceptance. It does not cover unlimited mode, save/load, streaming, meshes, or visual quality.

## Capture and compare

1. Build the same editor/automation target and exact source revisions on each machine. This fixture belongs to the uncooked automation module and is available only where development automation tests are compiled in. A shipping or dedicated-server target that excludes this module cannot run it; running an editor headlessly is useful but does not prove the dedicated-server binary path.
2. Record separately alongside each output: platform and architecture, OS, CPU, compiler and configuration, Unreal Engine version/revision, project and VoxelLand Git revisions including local changes, FastNoise source revision and build settings, automation command, and any relevant plugin settings. The capture does not derive or validate these metadata. Do not compare captures with different fixture or dependency revisions as though they represented identical inputs.
3. Start the editor commandlet in unattended, null-renderer mode, replacing paths and target executable with the local installation and choosing an **unused** output path in an existing writable directory:

   ```text
   UnrealEditor-Cmd <project.uproject> -unattended -nop4 -nullrhi -ExecCmds="Automation RunTests VoxelLand.M4.Determinism.Capture;Quit" -VoxelLandM4Capture="<absolute-output-path>"
   ```

   Inspect the automation result and process log; an output file alone is not a passing result. The test runs without the capture option but writes nothing. The option uses no-replace creation, so choose a new path for every run; it never blesses or updates an existing baseline. A failed save fails the automation test. Preserve logs and metadata with the output.
4. Compare the entire UTF-8 capture contents, for example using `git diff --no-index <host-a-output> <host-b-output>` or an equivalent byte comparison. A difference identifies the first reported stage or chunk to investigate; it is not itself proof which host is correct. Re-run on each host with new output paths and retain both sets. Do not call either result a golden baseline until independently reviewed.

This fixture enables native SSE2 overhang noise and fails if its worker cannot be created. The native SSE2 path is distinct from the scalar VM backend used by the control graphs; a successful capture does not compare or establish equivalence between those backends. Cross-host comparisons require the same native backend and matching source/build configuration, and cannot cover a host without it. Verify dedicated-server operation separately with a server target and real world-generation scenario after the project supplies an appropriate server test harness.
