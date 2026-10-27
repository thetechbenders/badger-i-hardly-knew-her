# BHIHKH! Studio

Studio is a **local-only browser interface** to the existing TOML validator and firmware C++ renderer. It supports visual identity, contact and portfolio editing, an Advanced TOML tab, managed portrait processing and native-size whole-screen previews.

**What it does not currently do:** build/export a firmware UF2, flash a badge, manage arbitrary files, or expose a network service for collaborators.

## Start it

Python 3.11+; from the repository root:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r tools/requirements.txt -r studio/requirements.txt
python -m studio.app --port 0
```

The server reserves its localhost port first, then prints a **private launch URL containing a token**. Open that exact URL in your browser. Never paste it in an issue, PR, public chat or log. A server restart creates a different token. The reconnect view accepts the newly printed URL while preserving unsaved edits still held in the tab.

Under Windows/WSL, check both WSL and Windows port ownership if an unexpected server responds. The recommended `--port 0` avoids choosing a busy fixed port. Do not port-forward Studio or expose it behind a public proxy.

## Editing model and persistence

- Studio initializes a private `local/studio/badge.toml` from the form template. Its sample portraits and any selected 1-bit portraits live under the same Git-ignored workspace.
- **Visual Editor:** identity, typed contacts, projects and QR options.
- **Advanced TOML:** unexposed options and comments remain editable. Some comments inside rebuilt contact/project tables may be normalized.
- **Validate & save:** runs server-side validation, then writes the saved workspace form. Invalid edits are not persisted.
- Unsaved portrait selection updates the editor's reference only; the existing saved portrait remains intact until successful Save. If edits change during an asynchronous operation, Studio rejects stale results rather than treating newer edits as saved.

The target selector swaps to the new target's **sample** portrait; it does not currently remember a custom portrait for each target. A later UI revision may change this.

## Portrait workshop

Choose a JPEG/PNG up to 10 MiB. Select a crop and tune gamma, black/white clipping and sharpening, then **Compare dithering**. Four 1-bit renderings are offered: threshold, Bayer 8×8, Floyd–Steinberg and Atkinson. Clicking **Use selected portrait** creates an immutable `portrait-<target>-<sha256>.png` in the private workspace and updates the in-memory TOML. Select **Validate & save** to persist the reference.

Studio uses a **104×128** composition on *both* badges. The standalone `scripts/build-badge.sh` workflow instead makes a 104×176 portrait for the Badger 2350. Do not confuse the two preview methods.

Studio does **not** keep the uploaded original photo. It may retain previous unused content-addressed processed portraits until you clean the private workspace.

## Previewing the finished screens

**Render badge screens** validates the unsaved document, builds/uses the existing host C++ renderer and shows native-pixel badge, card, QR, index and portfolio screens for the selected hardware. QR decode and text-fit gates must pass. This is not a physical optical display simulation.

The preview build directory is shared, with serialization inside a Studio server process. Avoid multiple independent Studio servers running against the same build tree.

## Tests and limits

```bash
python -m unittest tests_py.test_studio -v
node --test tests_js/test_studio_browser.cjs
```

Source of truth: [docs/STUDIO.md](../docs/STUDIO.md) and `studio/`. For firmware output, use [personalization](Personalization.md) and [builds](Builds-and-Artifacts.md). For a rejected token, start at [troubleshooting](Troubleshooting.md).
