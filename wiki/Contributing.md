# Contributing, reviews and releases

Contributions should preserve the project's constraints: native portable C++ core, target-specific hardware backends, no silent truncation of personalized text, deterministic artifacts, robust flash recovery and truthful hardware validation.

## Before coding

1. Read the relevant [architecture](../docs/ARCHITECTURE.md), [formats](../docs/FORMATS.md) and [design notes](../docs/DESIGN.md).
2. Identify the hardware target(s), current branch and intended behavior. Do not infer cross-target support just because the portable core compiles.
3. Preserve existing changes from collaborators. Use reviewable branches/commits; never force-push someone else's working branch merely to simplify the PR history.
4. Add host/unit/regression tests for settings boundaries, CLI parsers, state machines, previews and asynchronous browser operations when applicable.
5. Keep private person/portrait files and actual USB exports out of Git.

## Changes and proof

Run [host tests and both firmware builds](Builds-and-Artifacts.md). Validate `SHA256SUMS`, UF2 family and reserved address windows before flashing. For a target-dependent change, distinguish **CI success** from real-device acceptance and file a [dated hardware test record](Testing-and-Validation.md). Include failure modes and negative tests for recovery-sensitive modifications.

Don't “fix” lint metrics by adding boilerplate comments while omitting tests of data loss, concurrency or power safety. Treat generated assets and toolchain pins as controlled inputs; regenerate intentionally and verify.

## PR and stack etiquette

A stacked PR targets another feature branch. Verify the child contains the parent commits and that its PR diff shows only the **child's** changes. Merge dependency PRs in order, then retarget the next PR to `main` and recheck its effective diff, CI and mergeability.

This wiki is itself a staged documentation PR dependent on Studio, which depends on the battery PR. It must not claim the documentation was released or the firmware was hardware-tested simply because its draft checks pass. The [publishing procedure](Publishing-the-Wiki.md) intentionally requires review.

## Before a firmware release

- Determine exact target, hardware-tested commit(s), source pinning, version/commit metadata and public/private content selection.
- Verify host tests, screen text-fit and QR decode, firmware cross-builds, memory bounds and artifact checksums.
- Run the appropriate physical checklist on the **actual release UF2**, record provenance, regressions and unresolved hardware limitations.
- Package firmware with `scripts/package_firmware.py` and verify the package. Never upload private personalized UF2s or backups.
- Update README, wiki, source docs and changelog/release notes consistently, distinguishing estimated from measured behavior.

## Licensing

The repository [does not yet declare its own code license](../docs/LICENSES.md). Seek explicit permission before assuming redistribution rights. Third-party dependency notices are in [LICENSES.md](../docs/LICENSES.md). Existing collaborators and their credit should remain visible.

References: [review/hardening history](../docs/REVIEW.md), [licensing](../docs/LICENSES.md), [test records](../docs/test-records/README.md).
