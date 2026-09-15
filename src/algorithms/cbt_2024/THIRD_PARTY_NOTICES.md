# Third-party notices

## `large_cbt` / CBT 2024 reference implementation

RoamTesting uses the public [`AnisB/large_cbt`](https://github.com/AnisB/large_cbt) repository as a source-level semantic reference for the HPG 2024 CBT implementation. The frozen official reference is commit `7351e6fb380acc149b3aef22a6c39bf3df7950a6`; the local study checkout additionally contains commit `7ae736d179528a0996449c0cc2db7f3279edc8ee`, which only replaces NVIDIA-incompatible 64-bit `firstbithigh` calls.

The upstream repository contained no `LICENSE`, `COPYING`, SPDX declaration, or license statement in its README when reviewed on 2026-08-25. Its source therefore has no identified redistribution or derivative-work grant. The root MIT license of RoamTesting does not grant rights in upstream material and must not be interpreted as doing so.

Until the upstream authors provide an explicit compatible license or written permission:

- do not include the `third_party/large_cbt` checkout in a release package;
- treat `src/algorithms/cbt_2024` and the corresponding shaders as source-referenced research work whose public redistribution requires a separate rights review;
- keep public source releases of the CBT-derived portion blocked;
- allow local building, comparison, testing, and benchmark reproduction only within the permissions available to the operator.

This notice records repository evidence and release policy; it is not legal advice.
