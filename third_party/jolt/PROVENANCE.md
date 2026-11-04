# Jolt Physics provenance

This directory holds the Jolt Physics library. It is not a Lykoware-owned library: upstream copyright stays with Jorrit Rouwe under the MIT licence in [LICENSE](LICENSE). The only exceptions are the two Lykoware replacement files listed under "Local source patches".

| Field | Value |
| --- | --- |
| Name | Jolt Physics |
| Upstream | https://github.com/jrouwe/JoltPhysics |
| Tag | `v5.6.0` (released 2026-07-11) |
| Commit | `e77f175595e64cb44218cc9d9d56fc365ad0e36a` |
| Obtained by | `git clone --depth 1 --branch v5.6.0` of the upstream repository |
| Upstream-file sha256 | `02caa30c3ba4b5a33d94c4ae72dba79aa7eec93cbf1fdb867e71eb6b4e0d8a28`: the included files as upstream shipped them |
| Vendored-file sha256 | `8977e77caf9bd322418e8a19a40dbb6a064f9588762ee0eb8b06e337a24a0fb6`: the same files after the two local replacements |
| SPDX licence | MIT (556 of the 559 files in `Jolt/` carry `SPDX-License-Identifier: MIT`; the other three, a CMake file, a debugger display file and a diagram, fall under the repository licence) |
| Licence file | [LICENSE](LICENSE) |
| Decision | [ADR 0003](../../docs/adr/0003-physics.md) (accepted by the owner 2026-11-04); build integration in [ADR 0018](../../docs/adr/0018-jolt-integration.md) |
| Wrapper | Planned: `lykoware::physics` in the engine library, with no Jolt types in public headers. Until it exists, only tests include Jolt. |

## What is included

- `Jolt/`, the library source;
- `Build/CMakeLists.txt`, upstream's CMake entry point, which `cmake/VendorJolt.cmake` adds as a subdirectory with its options forced;
- `Build/README.md`, which documents those options;
- `LICENSE` and the upstream `README.md`.

## What was omitted

- Never compiled or used here: `Assets/`, `Docs/`, `HelloWorld/`, `JoltViewer/`, `PerformanceTest/`, `Samples/`, `TestFramework/` and `UnitTests/`. `Build/CMakeLists.txt` refers to them only when Jolt is the top-level project, which it never is here.
- The rest of `Build/`: platform scripts, and Android's Gradle wrappers (`gradlew*`, Apache-2.0).
- `ContributorAgreement.md`, `Doxyfile`, `doxygen-awesome.css`, `run_doxygen.bat` and `sonar-project.properties`.

`PROVENANCE.md` is a Lykoware record. It is not an upstream file.

## Local source patches

Two upstream files said they were "Taken and adapted from Numerical Recipes". That book's code is copyrighted and not under a permissive licence. On 2026-11-04 the owner chose to replace both (ADR 0003). Each replacement keeps the upstream function's name, template signature, contract and failure behaviour. Each is written from the standard algorithm in another textbook, not from the upstream body, and states that in its header.

| File | Upstream sha256 | Lykoware sha256 | Replacement |
| --- | --- | --- | --- |
| `Jolt/Math/EigenValueSymmetric.h` | `4b308c3d1bc7174d22c3d3a156647aec15cd45966dc8193432ddf345869ae48c` | `f4d77c28e110c1cdfc854963ada651b03b2f1290f222c223b7ebea1f197d6167` | Cyclic Jacobi method (Golub and Van Loan, *Matrix Computations*, §8.5) |
| `Jolt/Math/GaussianElimination.h` | `52e9e7bd4d40c8cd02125779f903690fd7381a286f28bf28b249cdd95c98ff58` | `47709a7000e4182c9bc428db40df187c2a94658d317487fa914212d0de9d6e98` | Gauss–Jordan elimination with partial (row) pivoting |

- **Copyright:** the replacement files are Lykoware code (copyright Joshua Kingsworth, see the repository `LICENSE`), not MIT. Offering them upstream would need the owner to license them.
- **Callers:**
  - `EigenValueSymmetric` is called by `MassProperties::DecomposePrincipalMomentsOfInertia`;
  - `GaussianElimination` is called by `Matrix::SetInversed`, for sizes other than 2×2, which use a closed formula, and by the wheeled-vehicle controller.
- **Tests:** `physics.jolt_math` (`tests/physics_jolt.cpp`) checks both replacements against known answers and properties.

**On a Jolt update:** check whether upstream has changed these two files. If it has replaced them under MIT, drop the local patch. Otherwise keep it, and confirm that no new file cites Numerical Recipes.

## Integrity check

From this directory, the vendored-file hash covers every file except `PROVENANCE.md`:

```sh
find . -type f ! -name PROVENANCE.md | LC_ALL=C sort | xargs sha256sum | sha256sum
```

Restoring the two upstream files gives the upstream-file hash.
