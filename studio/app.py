"""Local-only BHIHKH Studio Phase 1.

Security boundary: no arbitrary file requests, bind loopback, reject foreign Host
and Origin, require a per-process token on every API request. This does not
provide authentication against malware running as the same local user.
"""
from __future__ import annotations

import re
import secrets
import shutil
import sys
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import HTMLResponse, JSONResponse, Response
from pydantic import BaseModel, Field

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))
import badge_form  # noqa: E402
from studio import form_editor  # noqa: E402
from bhihkh_targets import TARGETS  # noqa: E402

STATIC = Path(__file__).resolve().parent / "static"
MAX_JSON = 128 * 1024


class Edit(BaseModel):
    target: str
    toml: str = Field(max_length=100_000)


class Compose(Edit):
    fields: dict


def _host_ok(host: str) -> bool:
    # Reject userinfo, arbitrary DNS aliases and other hostnames, including
    # attacker-controlled hosts that resolve to 127.0.0.1 (DNS rebinding).
    m = re.fullmatch(r"(localhost|127\.0\.0\.1|\[::1\])(?::[0-9]{1,5})?", host.lower())
    return m is not None


def _origin_ok(origin: str | None, host: str) -> bool:
    return origin is None or origin == f"http://{host}"


def _safe_references(text: str, target: str) -> list[str]:
    """Reject file references outside this managed, private Studio workspace.

    Parsing first prevents a malicious TOML string from bypassing the check
    through duplicate keys or alternative table spelling. The existing form
    validator handles the complete *content* schema after this sandbox check.
    """
    try:
        parsed = badge_form.parse_form(Path("badge.toml"), text)
    except badge_form.FormError as error:
        return list(error.problems)
    errors = []
    for section, keys in (("portrait", ("photo", "processed", "settings")),
                          ("qr", ("vcard_file",))):
        table = parsed.get(section, {})
        if not isinstance(table, dict):
            continue  # load_form emits the canonical schema error
        for key in keys:
            value = table.get(key)
            if value is None or value == "":
                continue
            permitted = f"sample-{target}.png" if section == "portrait" and key == "processed" else None
            if value != permitted:
                errors.append(f"{section}.{key}: Studio Phase 1 only supports its managed sample portrait; "
                              "arbitrary file references and uploads are not enabled yet")
    return errors


def create_app(workspace: Path | None = None) -> FastAPI:
    app = FastAPI(title="BHIHKH Studio", docs_url=None, redoc_url=None, openapi_url=None)
    token = secrets.token_urlsafe(32)
    app.state.studio_token = token  # Accessible only inside this Python process, not through HTTP.
    raw_work = workspace or ROOT / "local" / "studio"
    if raw_work.is_symlink():
        raise RuntimeError("Refusing symlinked Studio workspace")
    work = raw_work.resolve()
    if workspace is None and not work.is_relative_to((ROOT / "local").resolve()):
        raise RuntimeError("Studio workspace must be under local/")
    work.mkdir(parents=True, exist_ok=True)
    template = (ROOT / "config" / "badge-form.toml").read_text(encoding="utf-8")
    template = template.replace('processed = "../assets/sample/portrait_placeholder.png"',
                                'processed = "sample-badger2040.png"')
    if 'processed = "sample-badger2040.png"' not in template:
        raise RuntimeError("Badge form template changed; review Studio sample path")
    for target in TARGETS:
        sample = (ROOT / "assets" / "sample" /
                  ("portrait_placeholder.png" if target == "badger2040" else
                   "portrait_placeholder_badger2350.png"))
        output = work / f"sample-{target}.png"
        if output.is_symlink():
            raise RuntimeError("Refusing symlinked Studio asset")
        if not output.exists():
            shutil.copyfile(sample, output)
    form_path = work / "badge.toml"
    if form_path.is_symlink():
        raise RuntimeError("Refusing symlinked Studio form")
    if not form_path.exists():
        form_path.write_text(template, encoding="utf-8")
    # Parse TOML rather than matching a sample filename in comment text.
    saved = badge_form.parse_form(form_path)
    portrait = saved.get("portrait", {})
    selected = portrait.get("processed") if isinstance(portrait, dict) else None
    active = {"target": next((name for name in TARGETS
               if selected == f"sample-{name}.png"), "badger2040")}

    def validate(text: str, target: str) -> dict:
        if target not in TARGETS:
            return {"ok": False, "problems": ["target: unsupported hardware"], "notes": []}
        problems = _safe_references(text, target)
        if problems:
            return {"ok": False, "problems": problems, "notes": []}
        try:
            form = badge_form.load_form(form_path, text=text, target=target)
        except badge_form.FormError as error:
            return {"ok": False, "problems": list(error.problems), "notes": []}
        return {"ok": True, "problems": [], "notes": list(form.notes)}

    @app.middleware("http")
    async def enforce_local_boundary(request: Request, call_next):
        host = request.headers.get("host", "")
        if not _host_ok(host):
            return JSONResponse({"detail": "Host not allowed"}, status_code=403)
        if not _origin_ok(request.headers.get("origin"), host):
            return JSONResponse({"detail": "Origin not allowed"}, status_code=403)
        if request.url.path.startswith("/api/v1/"):
            if not secrets.compare_digest(request.headers.get("x-studio-token", ""), token):
                return JSONResponse({"detail": "Invalid session token"}, status_code=403)
        if request.method not in ("GET", "HEAD", "PUT"):
            return JSONResponse({"detail": "Method not allowed"}, status_code=405)
        response = await call_next(request)
        response.headers["Cache-Control"] = "no-store"
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["Referrer-Policy"] = "no-referrer"
        response.headers["Content-Security-Policy"] = ("default-src 'none'; script-src 'self'; "
                 "style-src 'self'; connect-src 'self'; img-src 'self' data:; base-uri 'none'; "
                 "form-action 'none'; frame-ancestors 'none'")
        return response

    @app.get("/")
    def home():
        html = (STATIC / "index.html").read_text(encoding="utf-8")
        return HTMLResponse(html)

    @app.get("/app.js")
    def javascript():
        return Response((STATIC / "app.js").read_text(encoding="utf-8"),
                        media_type="text/javascript")

    @app.get("/style.css")
    def stylesheet():
        return Response((STATIC / "style.css").read_text(encoding="utf-8"),
                        media_type="text/css")

    @app.get("/api/v1/targets")
    def targets():
        return {"targets": [{"id": t.name, "name": t.board, "display": [t.display_w, t.display_h],
                             "portrait_designed": [t.portrait_w, t.portrait_h],
                             "portrait_max_width": t.portrait_max_w}
                            for t in TARGETS.values()]}

    @app.get("/api/v1/workspace")
    def get_workspace():
        return {"target": active["target"], "toml": form_path.read_text(encoding="utf-8")}

    async def read_json(request: Request, model):
        """Bound each request as it streams; validate the typed API envelope."""
        if request.headers.get("content-type", "").split(";")[0].strip() != "application/json":
            raise HTTPException(415, "JSON required")
        if request.headers.get("content-length"):
            try:
                if int(request.headers["content-length"]) > MAX_JSON:
                    raise HTTPException(413, "Request too large")
            except ValueError:
                raise HTTPException(400, "Invalid Content-Length")
        chunks = bytearray()
        async for chunk in request.stream():
            if len(chunks) + len(chunk) > MAX_JSON:
                raise HTTPException(413, "Request too large")
            chunks.extend(chunk)
        try:
            return model.model_validate_json(bytes(chunks))
        except ValueError:
            raise HTTPException(422, "Invalid request") from None

    @app.put("/api/v1/parse")
    async def parse_view(request: Request):
        """Expose the editable model extracted from the supplied form text."""
        data = await read_json(request, Edit)
        try:
            fields = form_editor.view(data.toml)
        except (ValueError, TypeError) as error:
            return JSONResponse({"ok": False, "problems": [str(error)], "notes": []}, status_code=422)
        return {"ok": True, "fields": fields}

    @app.put("/api/v1/compose")
    async def compose_view(request: Request):
        """Create TOML using tomlkit; never persist or bypass canonical save validation."""
        data = await read_json(request, Compose)
        try:
            output = form_editor.apply(data.toml, data.fields)
        except (ValueError, TypeError) as error:
            return JSONResponse({"ok": False, "problems": [str(error)], "notes": []}, status_code=422)
        if len(output) > 100_000:
            raise HTTPException(413, "Composed form too large")
        # Canonical validation is performed on the save endpoint. Composition
        # allows unfinished edits so validation messages remain actionable.
        return {"ok": True, "toml": output}

    @app.put("/api/v1/workspace")
    async def put_workspace(request: Request):
        update = await read_json(request, Edit)
        check = validate(update.toml, update.target)
        if not check["ok"]:
            return JSONResponse(check, status_code=422)
        if form_path.is_symlink():
            raise HTTPException(409, "Unsafe workspace file")
        # Stage then replace rather than corrupt a saved form on partial write.
        staging = work / "badge.toml.tmp"
        if staging.is_symlink():
            raise HTTPException(409, "Unsafe staging file")
        staging.write_text(update.toml, encoding="utf-8")
        staging.replace(form_path)
        active["target"] = update.target
        return check

    return app


def main():
    import argparse
    import uvicorn

    parser = argparse.ArgumentParser(description="BHIHKH! Studio (localhost only)")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    app = create_app()
    # Fragment is never sent in HTTP requests. The browser reads it and
    # drops it from history; no unauthenticated token bootstrap endpoint.
    print(f"BHIHKH Studio: http://127.0.0.1:{args.port}/#token={app.state.studio_token}", flush=True)
    print("Keep this local launch URL private: it grants access to the workspace.", flush=True)
    uvicorn.run(app, host="127.0.0.1", port=args.port, access_log=False)


if __name__ == "__main__":
    main()
