# BHIHKH! Studio

A **local-only** browser shell around the existing TOML-form validation
pipeline. The visual editor now exposes identity, contacts, projects and QR options,
with move/add/remove controls. The full original TOML remains available in
Advanced mode. Portrait upload, four-method monochrome comparison and
firmware-accurate C++ screen previews are available. After validating and saving,
Studio can export a private `.badgerpack` input archive for a separate local
firmware build. Studio does **not** generate a UF2 or flash a device.
See [BADGERPACK.md](BADGERPACK.md) for the package format and build command.

## Start

Python 3.11+ is required. Install the existing host dependencies and the
isolated Studio web dependencies:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r tools/requirements.txt -r studio/requirements.txt
python -m studio.app --port 8765
```

### Port conflicts and multiple servers

Studio reserves its loopback listening socket **before** printing the private
launch URL. If another process already owns the requested port, Studio exits
with an explicit error, and does **not** print an unusable tokenized link.
Each process has a different token, so a link from another Studio process
cannot authenticate to the one your browser reached.

To avoid an occupied default port (8765), let the OS select a free port:

```bash
python -m studio.app --port 0
```

Open the **actual port** printed with the private launch link, not a
bookmarked 8765 address. Under WSL, Windows applications can sometimes also
hold a localhost port independently of the Linux-side listener. If a freshly
launched link is still rejected, check both Windows and WSL port ownership
before assuming the browser or badge form is broken. In WSL, for example:

```bash
ss -ltnp '( sport = :8765 )'
```

Do not kill another developer's server without checking what it is
doing. Close obsolete instances cleanly, or launch on a different port.

Open the **complete private launch URL** printed in the terminal, including
its `#token=...` fragment. The fragment is not transmitted in the HTTP
request. The browser removes the fragment from its address bar, verifies the token
with the running server, and retains it for the current tab only after
successful authentication. Every Studio server restart creates a new token:
a tab holding the previous token must reconnect. The editor now displays a
reconnect form rather than showing an empty, broken UI. Paste the **complete
private launch URL printed by the currently running server**, or the token
itself, into that form. Reconnecting preserves unsaved visual/TOML edits still
held in the same open tab. The token is never embedded in public HTML or
logged from form submissions. Anyone who obtains
the complete launch URL can access your workspace; do not share it.
The root page does not disclose the token. A separate local OS account
that can connect to loopback without the token cannot access the private
API, but malware with your OS account permissions is outside this model. On WSL, a host browser
may be able to reach the forwarded localhost port. Never port-forward Studio
to another machine or run it behind a public reverse proxy.

Studio keeps its editable form and sample portrait files under
`local/studio/`, which is ignored by Git. The server binds to
`127.0.0.1`, rejects arbitrary Host/Origin values, and requires a fresh
per-process token for all API access. Token material is printed to the
owner's launching terminal, not embedded in an unauthenticated page. It does **not** authenticate other
programs running as your local user.

The form starts from `config/badge-form.toml` with a bundled, non-personal
sample portrait for each target. Studio refuses arbitrary TOML file references; managed portrait import and
host-rendered badge screen previews are supported. Direct firmware builds, USB flashing,
and a filesystem browser are not implemented in Studio. Package export is available
only after saving. Only successfully validated TOML is saved; failed edits
stay in the browser, and the old saved form is retained.

### API contract

| Route | Phase 1 |
|---|---|
| `GET /api/v1/targets` | Targets, display and designed portrait dimensions |
| `GET /api/v1/workspace` | Saved TOML and active target |
| `PUT /api/v1/workspace` | Validate with `badge_form.load_form()`, then save atomically |
| `PUT /api/v1/parse` | Extract visual fields from a TOML document without saving it |
| `PUT /api/v1/compose` | Apply edited fields, keeping other TOML options and comments |
| `PUT /api/v1/portrait/preview` | Decode a bounded PNG/JPEG and return four in-memory 1-bit comparisons |
| `PUT /api/v1/portrait/select` | Create or reuse an immutable content-addressed native-size, 1-bit PNG (no saved TOML mutation) |
| `PUT /api/v1/portrait/reference` | Update only `[portrait].processed` in unsaved TOML, preserving comments and all other fields |
| `PUT /api/v1/screens/preview` | Validate unsaved TOML and render all screens with the existing C++ host renderer, text-fit and QR gates |
| `GET /api/v1/package` | Token-authenticated download of the validated **saved** TOML and its managed portrait as a private `.badgerpack` |
| Firmware UF2 export or flashing | Not implemented in Studio; use the separate local package build workflow |

Every API request must include `X-Studio-Token`, sourced from a verified private launch URL or reconnect form. Mutations require JSON and accept bounded payloads.

### Tests

```bash
python -m unittest tests_py.test_studio -v
```

The Studio tests cover target switching, validation, rejected input paths,
foreign hosts/origins, token checks, and non-destructive invalid edits.
The existing host and firmware CI remain the regression gates.

### Visual-editor notes

The editor keeps the existing TOML as the sole durable format. Unedited advanced
settings and non-visual QR properties (e.g. vCard text) survive visual changes.
Rebuilding contact/project tables may normalize comments *inside* those tables;
other TOML comments/sections survive. Invalid edits cannot be saved. Portraits can be imported through the managed workshop; sample and selected
portraits remain in the local private workspace.

### Portrait workshop

Choose a PNG or JPEG no larger than 10 MiB, then **Compare dithering**.
Studio creates four monochrome comparisons using the existing portrait converter:
threshold, Bayer 8×8, Floyd–Steinberg, and Atkinson. Choosing a comparison
and clicking **Use selected portrait** stores only that processed 1-bit PNG
under a content-addressed filename such as `portrait-badger2040-<sha256>.png`
in the private Studio workspace. Existing selected portraits are never overwritten:
choosing a different rendering without saving leaves the saved badge unchanged.
Finish with **Validate & save** to persist the new TOML reference. The original
uploaded photo is never retained. Existing pre-V1 `portrait-<target>.png` files
remain readable; unused managed portraits may remain in the local workspace
until manually cleaned up. Target switching edits only `[portrait].processed`,
not filename-like text in other TOML fields or comments.

The workshop starts with an automatic center crop at the 104×128
composition on both devices. A visual crop rectangle can be dragged and resized with a bottom-right handle; numeric
X/Y/width/height fields remain available for precision. All crop paths retain
a 13:16 aspect ratio and the server validates the coordinates. Gamma (0.2–5),
black and white clipping (0–40%) and sharpen (0–3) use the existing
Python converter, not an independent browser approximation. Comparing regenerates
all four methods; clicking a method displays a 104×128 pixel 1:1 CSS preview.
Physical e-paper size and optical appearance are not simulated. Badge 2350 still supports its taller target
elsewhere in the existing converter, but the Studio workflow intentionally
preserves the original shorter portrait composition. Firmware-accurate whole-screen previews and private package export are
implemented. Studio does not compile or flash the firmware itself.

### Firmware-accurate screen previews

Click **Render badge screens** to validate and render the current visual or Advanced
TOML edits without saving. The existing `badge_form.preview()` and C++ host
renderer generate native monochrome screens for the selected hardware target,
including badge, contact card, QR, project index, and configured project screens.
Existing text-fit and QR decoding checks must pass; otherwise Studio reports
validation errors without pretending the output is ready. Rendering can take
longer the first time because the host executable is built as needed.

The host CMake/Ninja build directories are shared and preview generation is
serialized within a Studio process. The intermediate files are temporary,
stored beneath the private workspace, and deleted after the preview request.
Do not run multiple separate Studio processes against the same host build
directories. No device flashing, firmware build, or workspace save occurs.

## Exporting and building a private badge package

1. Select the correct hardware target and finish the visual or Advanced TOML edits.
2. Click **Validate & save**. Export uses the saved workspace only, never unsaved browser changes.
3. Click **Export .badgerpack**. Keep the archive private: it includes your name,
   contacts, links and processed portrait. Studio does not include the original
   source photograph, an admission credential or firmware binaries.
4. Transfer the archive to a checkout with the pinned build dependencies.
   From that checkout, build without flashing:

   ```bash
   scripts/build-badge.sh --package /path/to/badge.badgerpack
   ```

5. Inspect `local/out/badgerpack-<sha12>/previews/` and
   `local/out/badgerpack-<sha12>/fw/`. The build verifies text fit, QR decoding,
   target-specific UF2 layout and artifact hashes. Its output directory also
   contains `badgerpack-provenance.json`, with the full package SHA-256, target,
   and input manifest. The `fw/SHA256SUMS` file covers firmware artifacts,
   not that provenance JSON.
6. Preserve the original photo and the private package separately. Before
   flashing, follow the [board-specific installation and recovery instructions](INSTALL.md).

The Badger 2040 firmware is `badger_badge.uf2`; the Badger 2350 firmware is
`badger2350_badge.uf2`. Each has a separately flashable `-assets.uf2`.
Do **not** substitute one board's images for the other. This package-based
workflow preserves the workshop-selected **104×128** portrait on both boards.
The bundled Badger 2350 sample placeholder remains **104×176** and is preserved
at that size if exported without selecting a custom portrait. The standalone
form/photo workflow can also generate a **104×176** portrait for Badger 2350;
it is a different processing path.

Package exports are deterministic for identical saved inputs, and CI checks
byte-identical sample firmware across two independent package output directories.
That does not replace a reproducibility check of your own private artifact or
physical testing of the exact flashed build.
