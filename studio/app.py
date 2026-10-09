"""Local-only BHIHKH Studio Phase 1.

Security boundary: no arbitrary file requests, bind loopback, reject foreign Host
and Origin, require a per-process token on every API request. This does not
provide authentication against malware running as the same local user.
"""
from __future__ import annotations

import base64
import binascii
import io
import tempfile
import re
import secrets
import socket
import shutil
import sys
from pathlib import Path

from PIL import Image, ImageOps, UnidentifiedImageError

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import HTMLResponse, JSONResponse, Response
from pydantic import BaseModel, Field

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))
import badge_form  # noqa: E402
import portrait as portrait_tool  # noqa: E402
from studio import form_editor  # noqa: E402
from bhihkh_targets import TARGETS  # noqa: E402

STATIC = Path(__file__).resolve().parent / "static"
MAX_JSON = 128 * 1024
MAX_PORTRAIT_BYTES = 10 * 1024 * 1024
MAX_PORTRAIT_JSON = 15 * 1024 * 1024


class Edit(BaseModel):
    target: str
    toml: str = Field(max_length=100_000)


class Compose(Edit):
    fields: dict


class PortraitRequest(BaseModel):
    target: str
    image: str = Field(max_length=14_000_000)
    crop: list[int] | None = None


class PortraitSelection(BaseModel):
    target: str
    image: str = Field(max_length=200_000)


def decode_image(value: str) -> bytes:
    try:
        raw = base64.b64decode(value, validate=True)
    except (ValueError, binascii.Error):
        raise HTTPException(422, "Invalid base64") from None
    if len(raw) > MAX_PORTRAIT_BYTES:
        raise HTTPException(413, "Image exceeds 10 MiB")
    return raw


def output_size(target: str) -> tuple[int, int]:
    if target not in TARGETS:
        raise HTTPException(422, "Unsupported target")
    return TARGETS[target].portrait_w, min(TARGETS[target].portrait_h, 128)


def image64(image: Image.Image) -> str:
    stream = io.BytesIO()
    image.save(stream, format="PNG")
    return base64.b64encode(stream.getvalue()).decode("ascii")


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
            permitted = (f"sample-{target}.png", f"portrait-{target}.png") if section == "portrait" and key == "processed" else ()
            if value not in permitted:
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
               if selected in (f"sample-{name}.png", f"portrait-{name}.png")), "badger2040")}

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

    async def read_json(request: Request, model, limit=MAX_JSON):
        """Bound each request as it streams; validate the typed API envelope."""
        if request.headers.get("content-type", "").split(";")[0].strip() != "application/json":
            raise HTTPException(415, "JSON required")
        if request.headers.get("content-length"):
            try:
                if int(request.headers["content-length"]) > limit:
                    raise HTTPException(413, "Request too large")
            except ValueError:
                raise HTTPException(400, "Invalid Content-Length")
        chunks = bytearray()
        async for chunk in request.stream():
            if len(chunks) + len(chunk) > limit:
                raise HTTPException(413, "Request too large")
            chunks.extend(chunk)
        try:
            return model.model_validate_json(bytes(chunks))
        except ValueError:
            raise HTTPException(422, "Invalid request") from None

    @app.put("/api/v1/portrait/preview")
    async def preview_portrait(request: Request):
        data = await read_json(request, PortraitRequest, MAX_PORTRAIT_JSON)
        size = output_size(data.target)
        raw = decode_image(data.image)
        try:
            with Image.open(io.BytesIO(raw)) as source:
                if source.format not in ("JPEG", "PNG"):
                    raise HTTPException(422, "Only JPEG and PNG are supported")
                if (source.width < 32 or source.height < 32
                        or source.width * source.height > 12_000_000):
                    raise HTTPException(422, "Image dimensions out of range")
                source.load()
                image = ImageOps.exif_transpose(source).convert("RGB")
        except (OSError, UnidentifiedImageError, Image.DecompressionBombError):
            raise HTTPException(422, "Invalid image") from None
        if data.crop is None:
            cw = min(image.width, int(image.height * size[0] / size[1]))
            ch = round(cw * size[1] / size[0])
            crop = [(image.width - cw) // 2, (image.height - ch) // 2, cw, ch]
        else:
            crop = data.crop
        if (len(crop) != 4 or any(type(n) is not int for n in crop)
                or crop[0] < 0 or crop[1] < 0 or crop[2] < 8 or crop[3] < 8
                or crop[0] + crop[2] > image.width or crop[1] + crop[3] > image.height
                or abs(crop[2] / crop[3] - size[0] / size[1]) > 0.025):
            raise HTTPException(422, "Invalid crop or crop aspect")
        with tempfile.TemporaryDirectory(dir=work) as temp:
            source_path = Path(temp) / "source.png"
            image.save(source_path)
            try:
                gray = portrait_tool.load_gray(source_path, tuple(crop), size, 1.0, 1.0, 2.0, 0.6)
                variants = {method: image64(portrait_tool.to_image(portrait_tool.convert(gray, method)))
                            for method in portrait_tool.METHODS}
            except (ValueError, SystemExit) as error:
                raise HTTPException(422, str(error)) from None
        return {"width": image.width, "height": image.height, "crop": crop,
                "output": list(size), "variants": variants}

    @app.put("/api/v1/portrait/select")
    async def select_portrait(request: Request):
        data = await read_json(request, PortraitSelection)
        size = output_size(data.target)
        raw = decode_image(data.image)
        try:
            with Image.open(io.BytesIO(raw)) as image:
                image.load()
                if image.format != "PNG" or image.mode != "1" or image.size != size:
                    raise HTTPException(422, "Expected native-size 1-bit PNG")
        except (OSError, UnidentifiedImageError, Image.DecompressionBombError):
            raise HTTPException(422, "Invalid processed image") from None
        name = f"portrait-{data.target}.png"
        path = work / name
        stage = work / (name + ".tmp")
        if path.is_symlink() or stage.is_symlink():
            raise HTTPException(409, "Unsafe portrait path")
        stage.write_bytes(raw)
        stage.replace(path)
        return {"ok": True, "processed": name}

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


def _bind_local_port(port: int) -> socket.socket:
    """Reserve loopback before advertising a session token or launch URL.

    Uvicorn normally binds after the startup code runs. If a stale server is
    already listening, printing before that bind advertises a fresh token
    for a second server that cannot start.
    """
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        listener.bind(("127.0.0.1", port))
        listener.listen(128)
    except OSError:
        listener.close()
        raise
    return listener


def main():
    import argparse
    import uvicorn

    parser = argparse.ArgumentParser(description="BHIHKH! Studio (localhost only)")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error("port must be 0..65535 (0 selects a free port)")
    try:
        listener = _bind_local_port(args.port)
    except OSError as error:
        parser.exit(2, f"Studio could not bind 127.0.0.1:{args.port}: {error}\n"
                    "Another Studio or other server may already be listening.\n"
                    "Stop that server, or use --port 0 for an automatically selected port.\n")
    try:
        # Display the actual reserved port, including for --port 0.
        port = listener.getsockname()[1]
        app = create_app()
        server = uvicorn.Server(uvicorn.Config(
            app, host="127.0.0.1", port=port, access_log=False))
        # The port is reserved before disclosing the session credential.
        # The URL fragment is not sent with HTTP requests.
        print(f"BHIHKH Studio: http://127.0.0.1:{port}/#token={app.state.studio_token}", flush=True)
        print("Keep this private launch URL secret; it grants workspace access.", flush=True)
        server.run(sockets=[listener])
    finally:
        listener.close()


if __name__ == "__main__":
    main()
