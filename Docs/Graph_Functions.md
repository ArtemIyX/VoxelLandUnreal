# Reusable graph functions

A VoxelLand Graph Function is a reusable typed part of a terrain graph. A function is source data: the calling graph compiles it into its own program. The function does not start terrain generation or have an independent runtime program.

## Create a function

1. In the Content Browser, create a **VoxelLand Graph Function** and open it.
2. Add Float, Int or Bool parameters in the Details / Parameters panel. Each parameter has a stable ID and becomes one call input. Parameter names are unique ASCII identifiers, beginning with a letter or underscore; use the parameter-reference node context menu to select a parameter by ID.
3. Build the function graph using the normal nodes and parameter-reference nodes. Select the node that supplies the result, then use **Set selected function output** above the graph and choose its output pin. The selected pin determines the result type: Float, Int or Bool. Density filter outputs are not supported as function results.
4. Save the function. The function editor does not run a terrain preview of the function by itself; place a call in a terrain graph to preview its result.

A function has one result and at most 63 inputs. The parameter defaults become the initial unconnected defaults on a new call; a connected call input supplies that value instead. Parameters on a function are its inputs, not implicit world settings. Connect a world parameter to a call input when a world override should affect the function.

## Call and update a function

In a terrain graph or another function, add the saved function from **Graph functions** in the node palette or graph context menu. Dragging from a compatible pin can connect the new call automatically. Input pins are matched to the function interface by stable IDs. An unconnected input uses its displayed call-site default; changes to the original parameter default do not overwrite an existing call-site value.

After changing the input list or a result type, right-click each existing call and choose **Refresh function interface**. Matching input IDs retain their pin IDs and compatible connections. Removing an input or changing an input or output type breaks incompatible links. A renamed input retains its ID and existing compatible links. The compiler reports an incompatible interface until affected calls are refreshed. Check the compiler results after refreshing.

Functions may call other functions. Dependency cycles, missing assets and incompatible interfaces produce errors; recursive calls are not supported. The current limits are 16 nested function calls, 128 distinct function assets, 4096 captured/expanded nodes, 16384 captured/expanded links and 1 MiB of aggregate operator configuration. These limits are checked when capturing or compiling the calling graph. Independently placed calls receive deterministic, distinct namespaces for their internal nodes and noise seeds.

Changing a referenced function invalidates the calling graph's compiled result. Open calling editors check dependencies and recompile their preview; save or cook the calling graph to produce an updated payload. Runtime initialization validates the captured source and compiled payload and reports a stale or missing dependency instead of substituting an older program. Keep function assets available to the cook alongside calling graphs.

## Selected-node preview

Use **Preview selected output** in the terrain graph preview to inspect a selected float output as density. This creates a transient compiled snapshot and does not change the graph's saved output or wiring. Return with **Preview graph output**. Integer, Bool and density filter outputs cannot be selected directly for this preview. Errors elsewhere in the graph can still prevent preview compilation.

The implementation has not been validated by a build, automated tests, a cook, a dedicated-server run or an interactive editor session as part of this change. Verify these workflows in the target Unreal Engine environment before relying on them.
