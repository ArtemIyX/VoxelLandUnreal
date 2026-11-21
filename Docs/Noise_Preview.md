# 2D noise preview

Select exactly one Perlin, fBm, Value or Simplex noise node in a terrain graph, then choose **2D noise: graph-wired** or **2D noise: standalone** in the Preview panel. Selection alone does not start a preview. Use **Preview graph output** to return to the terrain preview.

Graph-wired mode evaluates the selected noise output with its existing graph connections. It samples the world XY plane at the displayed world Z; unconnected coordinate inputs keep their graph defaults, so the image can legitimately be constant. Standalone mode temporarily supplies world X and Y to the selected node's coordinate inputs, and world Z for a 3D noise node. Its other inputs, node seed identity, graph seed and parameter overrides are unchanged. The substitution exists only in the copied preview graph and does not alter the asset or its wires.

The image is 128 by 128 samples centered at the world origin. Spacing is centimeters per pixel; smaller spacing zooms in. X increases to the right and Y increases upward. World Z is in centimeters. The fixed display maps -1 to black, 0 to gray and +1 to white; values outside that range are clipped, not automatically normalized. The status reports the observed raw minimum and maximum and the number of clipped samples. A stale or failed preview retains the previous image with a stale/error status.

Direct preview inside a function asset is unavailable because its noise seed is namespaced by the particular call site. The 2D action currently accepts built-in noise nodes placed directly in a terrain graph; it cannot inspect a noise node inside a function instance or the output of a function call. Native FastNoise still requires its supported SSE2 backend; raw scalar VM sampling does not supply a separate scalar native-noise backend.

The implementation has not been verified by a build, automated tests or an interactive rendering session as part of this change.
