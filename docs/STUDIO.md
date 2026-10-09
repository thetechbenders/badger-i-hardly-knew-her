# BHIHKH! Studio (visual editor in progress)

A **local-only** browser shell around the existing TOML-form validation
pipeline. The visual editor now exposes identity, contacts, projects and QR options,
with move/add/remove controls. The full original TOML remains available in
Advanced mode. Portrait upload and four-method monochrome comparison are available. Compiled firmware-renderer previews and artifact exports are **not implemented yet**.

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
sample portrait for each target. Studio currently refuses arbitrary TOML file
references; there is no portrait import, firmware build, USB flashing, or
filesystem browser. Only successfully validated TOML is saved; failed edits
stay in the browser, and the old saved form is retained.

### API contract

| Route | Phase 1 |
|---|---|
| `GET /api/v1/targets` | Targets, display and designed portrait dimensions |
| `GET /api/v1/workspace` | Saved TOML and active target |
| `PUT /api/v1/workspace` | Validate with `badge_form.load_form()`, then save atomically |
| `PUT /api/v1/parse` | Extract visual fields from a TOML document without saving it |
| `PUT /api/v1/compose` | Apply edited fields, keeping other TOML options and comments |
| `PUT /api/v1/portrait/preview` | Decode a bounded PNG/JPEG and return four in-memory 1-bit comparisons |\n| `PUT /api/v1/portrait/select` | Stage a selected native-size, 1-bit PNG in the managed workspace |\n| Firmware screen preview jobs and exports | Not yet implemented |

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
other TOML comments/sections survive. Invalid edits cannot be saved. Portrait
files cannot yet be imported; sample assets remain managed and local.

### Portrait workshop

Choose a PNG or JPEG no larger than 1 MiB, then **Compare dithering**.
Studio creates four monochrome comparisons using the existing portrait converter:
threshold, Bayer 8×8, Floyd–Steinberg, and Atkinson. Choosing a comparison
and clicking **Use selected portrait** stores only that processed 1-bit PNG
under the private Studio workspace. Finish with **Validate & save** to persist
the TOML reference. The original uploaded photo is never retained.

The initial workshop uses an automatic center crop at the native 104×128
composition on both devices; manual drag-to-crop and tonal adjustment controls
are not included in this slice. Badge 2350 still supports its taller target
elsewhere in the existing converter, but the Studio workflow intentionally
preserves the original shorter portrait composition. Existing firmware-accurate
whole-screen previews and export remain later milestones.
