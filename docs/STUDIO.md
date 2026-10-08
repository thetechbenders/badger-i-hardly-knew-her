# BHIHKH! Studio (visual editor in progress)

A **local-only** browser shell around the existing TOML-form validation
pipeline. The visual editor now exposes identity, contacts, projects and QR options,
with move/add/remove controls. The full original TOML remains available in
Advanced mode. Portrait upload, compiled firmware-renderer previews and
artifact exports are **not implemented yet**.

## Start

Python 3.11+ is required. Install the existing host dependencies and the
isolated Studio web dependencies:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r tools/requirements.txt -r studio/requirements.txt
python -m studio.app --port 8765
```

Open the **complete private launch URL** printed in the terminal, including
its `#token=...` fragment. The fragment is not transmitted in the HTTP
request. The browser removes it from its address bar after loading, and
includes the token only in same-origin API requests. Anyone who obtains
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
| Portrait upload, preview jobs, exports | Not yet implemented |

Every API request must include `X-Studio-Token`, sourced from the locally
served page. Mutations require JSON and accept bounded payloads.

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
