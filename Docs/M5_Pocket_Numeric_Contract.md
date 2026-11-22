# M5 pocket value-noise numeric contract, version 1

This describes the isolated scalar field only. It does not authorize carving or prove roof, protection, water, route clearance, mesh safety, or cross-host behavior. The production caller must enforce those rules against the final field. The no-pocket path must bypass this field entirely. The sample value is dimensionless signed Q15-style integer in `[-32768,32767]`; a threshold, if used by a later versioned composition stage, uses the same integer units and a strict `value > threshold` comparison. Converting a threshold excess into density or distance is not defined here.

Inputs are global XYZ signed integer centimetres in the inclusive interval `[-1048576,1048576]` and an integer lattice period `P` in `[1,1048576]` cm. For each axis, `cell = floor(position / P)` and `remainder = position - cell*P` in `[0,P)`. The upper corner may lie outside the supported sample-coordinate envelope; its integer lattice index is still defined and encoded. `weight = floor(remainder*65536/P)`, giving `[0,65535]`. Coordinate and period limits make every intermediate and corner index representable in signed 64 bits; do not use truncating division for negative positions.

Each corner uses this fixed **68-byte** packet, hashed with SHA-256 without an alternate provider or fallback. All integers are fixed-width little-endian; signed values use their 64-bit two's-complement bit encoding. The tag and version separate this field from all other streams.

| Offset | Width | Field |
|---|---:|---|
| 0 | 8 | ASCII `VLM5PCK` followed by `00` |
| 8 | 4 | u32 version 1 |
| 12 | 8 | i64 world seed |
| 20 | 16 | FGuid words A, B, C, D, each u32 |
| 36 | 8 | u64 salt |
| 44 | 24 | i64 lattice X, Y, Z, in order |

Interpret digest bytes 0 and 1 as a little-endian unsigned 16-bit word and subtract 32768. Linearly interpolate the eight corner values in fixed order X, then Y, then Z. For each pair use `floor((a*(65536-w)+b*w)/65536)` with mathematical floor, including negative numerators. The intermediate magnitude is bounded by `32768*65536`; signed 64-bit is sufficient. This is deterministic integer value noise, not a claim of exact SDF, smooth differentiability, physical safety or cross-host verification. Parameters and this version must participate in the eventual world identity.

Using seed `-1`, GUID words `(01234567,89abcdef,10203040,fedcba98)` in hexadecimal and salt `0102030405060708` hex, independent Python `hashlib`/`struct` reference calculations give:

- Corner `(0,0,0)`: packet `564c4d3550434b0001000000ffffffffffffffff67452301efcdab894030201098badcfe0807060504030201000000000000000000000000000000000000000000000000`, digest `e1e078d4af66428b1f28886a2f80b04add0d2326833d0075ac530ab26c4bbfff`, value `24801`.
- Corner `(-1,2,-3)`: packet `564c4d3550434b0001000000ffffffffffffffff67452301efcdab894030201098badcfe0807060504030201ffffffffffffffff0200000000000000fdffffffffffffff`, digest `9fc1b690346e19b93bfac6bbc925c564409c4267144035d61747b3ca0e04bd77`, value `16799`.
- Samples `(X,Y,Z,P)` in centimetres: `(0,0,0,16) -> 24801`, `(8,8,8,16) -> 11510`, `(-1,-17,25,16) -> 11499`, `(1048576,-1048576,0,1024) -> -16330`, `(15,0,0,16) -> 28032`, `(16,0,0,16) -> 28248`.

No build, automation, seam, performance, cook, server or cross-host run is implied by these reference calculations. The current scalar sampler makes eight SHA-256 calls and temporary allocations per sample; its performance is unmeasured and may be unsuitable for production chunk generation without a separately verified optimization. Its repeatability fixtures do not establish chunk-overlap seam behavior, which requires generation integration tests.
