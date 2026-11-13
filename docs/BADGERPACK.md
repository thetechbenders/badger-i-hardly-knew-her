# Studio badge packages (.badgerpack)

BHIHKH! Studio exports a local-only, versioned badge project ZIP. This is a
private **input** package, not firmware and not an admission ticket.

1. Open Studio, choose Badger 2040 or 2350, customize and **Validate & save**.
2. Choose **Export .badgerpack** and save the file privately.
3. Transfer the single file to a checkout with the pinned build dependencies.
4. Build without flashing:

```bash
scripts/build-badge.sh --package /path/to/badge.badgerpack
```

The command validates the archive, hashes, target, managed portrait and canonical
badge form, then runs the existing portrait/profile/QR/text-fit/firmware/artifact
pipeline. Results are under ignored `local/out/badgerpack-<sha12>/`; the
firmware is in `fw/` as `badger_badge.uf2` (2040) or
`badger2350_badge.uf2` (2350), with matching `-assets.uf2` and `SHA256SUMS`.
The output root contains `badgerpack-provenance.json` recording the complete
package SHA-256, board target and package manifest. The provenance file is **not**
listed in `fw/SHA256SUMS`, which covers the firmware artifacts. No USB writes occur.

The archive contains exactly `manifest.json`, `badge.toml`, and one
managed, target-specific, processed portrait PNG. Entries have fixed ZIP
timestamps and ordering, so identical input bytes produce an identical archive. The manifest specifies
`format: bhihkh.badgerpack`, integer `version: 1`, target, and SHA-256
hashes for both data members. The import rejects unexpected or duplicate
files, symlinks, traversal, missing data, oversized content, unknown format
versions, unsupported compression methods and targets, incorrect checksums,
and malformed compressed streams. It performs no
arbitrary filesystem extraction: the validated members are written to a
private temporary staging directory and removed after the build.

The export uses the **saved** Studio document, not unsaved visual/editor
state. A browser session token is required for the download, and Studio
stays bound to localhost. If the editor has unsaved changes, save first.
The package can contain personal names, contacts, links and your image:
keep it private, out of Git, public CI and support reports.

**Not included:** original source JPEG, custom external files, real Formnext
admission tokens, firmware binaries, secrets, or the standalone 2350
104×176 portrait pipeline. Studio's selected processed image currently uses
104×128 on both boards. This package intentionally preserves the exact
Studio-selected portrait and does not silently change its framing.

This workflow does not replace a recoverable backup of original portrait
sources and build provenance. Preserve your source photograph separately.
