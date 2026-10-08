# BHIHKH! Studio (Phase 1)

A **local-only** browser shell around the existing TOML-form validation
pipeline. This initial slice is an editable TOML view, target selection and
validation; the full visual editor, portrait upload, firmware-renderer
previews and artifact exports are **not implemented yet**.

## Start

Python 3.11+ is required. Install the existing host dependencies and the
isolated Studio web dependencies:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install -r tools/requirements.txt -r studio/requirements.txt
python -m studio.app --port 8765
```

Open `http://127.0.0.1:8765/` in your browser. On WSL, a host browser
may be able to reach the forwarded localhost port. Never port-forward Studio
to another machine or run it behind a public reverse proxy.

Studio keeps its editable form and sample portrait files under
`local/studio/`, which is ignored by Git. The server binds to
`127.0.0.1`, rejects arbitrary Host/Origin values, and requires a fresh
per-process token for all API access. It does **not** authenticate other
programs running as your local user.

The form starts from `config/badge-form.toml` with a bundled, non-personal
sample portrait for each target. Studio Phase 1 refuses arbitrary TOML file
references; there is no portrait import, firmware build, USB flashing, or
filesystem browser. Only successfully validated TOML is saved; failed edits
stay in the browser, and the old saved form is retained.

### API contract

| Route | Phase 1 |
|---|---|
| `GET /api/v1/targets` | Targets, display and designed portrait dimensions |
| `GET /api/v1/workspace` | Saved TOML and active target |
| `PUT /api/v1/workspace` | Validate with `badge_form.load_form()`, then save atomically |
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
