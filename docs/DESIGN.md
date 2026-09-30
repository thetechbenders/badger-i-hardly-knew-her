# Design notes

All previews come from the firmware renderer compiled for the host
(`host/preview.cpp`). They use the same fonts, coordinates and framebuffer
bits the badge sends to the panel. `docs/previews/native/` holds 296 × 128
images at native size; `x3/` holds 3× nearest-neighbour enlargements.

## Layout candidates

| A: portrait left (default) | B: portrait right |
|---|---|
| ![A](previews/x3/badge_layoutA.png) | ![B](previews/x3/badge_layoutB.png) |

- **A** puts a 104 × 128 portrait flush left. Next to it, the name is set in
  bold 24 px, falling back to 20 and then 17 px condensed before ellipsizing.
  Below that come the title in bold 12 px, the affiliation, a short rule, and
  the interests in 10 px. The event name sits in a black strip along the
  bottom of the text column.
- **B** mirrors the portrait to the right. It opens with a small event
  kicker, and the interests sit white-on-black in a band anchored to the
  bottom edge.

**Chosen default: A.** Both give the name the same 174 px column. A reads
left to right from face to name, which puts the most recognisable element
first. The name is the first text line, with nothing competing above it,
and the strong black element (the event strip) stays at the bottom, away
from the face. B remains available as
`layout 1`, and a long press on UP swaps layouts for the session, so both
can be compared on the real panel.

Every screen reserves a 56 × 8 px status area at the top right of its main
column, with no content rows 0–7 there. It holds the battery meter and the
gesture indicator (`build/previews/*/status_states_x6.png`). A host test
renders every screen with worst-case text and checks that the area stays
free and that the status never draws outside it.

Long titles wrap onto two lines of the same bold font instead of shrinking,
e.g. "Principal Mechanical Design / Engineer".

Screens without a portrait asset fall back to a full-width text layout.
Empty optional fields (affiliation, interests, event, contacts, project
tagline/link) are skipped entirely: no labels, rules or gaps are left behind.
A card with no contacts shows only the identity block. Project taglines
that do not fit one line wrap onto two lines of bold 10 instead of being cut.

`scripts/run-host-tests.sh` also writes `build/previews/diagnostic-max/`, a
**HOST DIAGNOSTIC SAMPLE**. Every text field is filled to its byte limit with
labelled filler, with LOW battery, a gesture fault and fault-state
diagnostics. It checks layout limits; it is not content.

## Portrait conversion

Source: the studio headshot `download_20180329_102401_Original.jpeg`
(600 × 600). The original is never modified. The processed files stay in
`local/`, which Git ignores.

Settings (`local/private/portrait.json`):

```json
{"crop": [118, 48, 364, 448], "size": [104, 128], "white_pct": 14, "black_pct": 2,
 "gamma": 0.7, "sharpen": 1.0, "method": "atkinson"}
```

The crop covers the head and upper shoulders at the target's 104:128 aspect.
The pipeline is: downscale in linear light (so the thin glasses frames keep
their weight), apply a mild unsharp mask, apply global levels (2 % / 14 %
clip, so the light studio background becomes clean white), apply gamma 0.7
to lift skin mid-tones, then convert to 1 bit. Only global tone and geometric
operations are used; nothing is painted, retouched or synthesised.

`tools/portrait.py --compare DIR` writes all four methods at native
resolution, plus a gray reference and a 3× sheet:

| Method | Result on this photo |
|---|---|
| Otsu threshold | Clean, bold graphic; glasses clear. Loses all shading, so the face flattens into outline. |
| Bayer 8×8 ordered | Keeps tone but adds a visible cross-hatch texture over the face; glasses break up. |
| Floyd–Steinberg | Most tonal detail, but dense speckle on skin makes the face look noisy at 1:1. |
| **Atkinson** (chosen) | Discards 25 % of the error, so highlights stay clean and skin is lightly stippled. The glasses, eyes, smile and jacket edge stay distinct. |

The comparison sheet for the real photo is at
`local/previews/portrait/compare_x3.png` and is not committed. The backup
portrait (`IMG_5860.jpeg`, gray ILS jacket) works with the same tool: new
crop coordinates are needed because it is 1932 × 2576.

## QR codes

- Encoded on the badge with Nayuki qrcodegen from `qr.payload`, so a code
  always matches the configured text. The QR code is never drawn from a stored
  image.
- Error correction M (raised automatically when free), falling back to L only
  if M cannot fit. The module size is an integer, at least 2 px (0.45 mm on
  this panel), chosen as the largest scale that fits 128 px with a
  **4-module white quiet zone** drawn into the bitmap.
- Typical sizes: a 23-byte `https://` URL is version 2 at 3 px/module (99 px).
  A ~150-byte vCard is version 7–8 at 2 px/module.
- A short HTTPS URL is the recommended destination: bigger modules scan
  faster at arm's length, and the details can be updated without reflashing.
  An offline vCard also decodes (tested), but at 2 px modules it needs a
  closer, steadier phone.
- An empty payload draws **nothing**: no box and no placeholder text. The
  card's text column then spans the full width, and the full-screen QR is
  not offered. An over-long payload (a configuration error) shows a dashed
  **QR PAYLOAD TOO LONG** box.
- Provisional local default: a trimmed offline vCard. A GitHub-link
  alternative is kept for side-by-side phone scanning; the final choice is
  pending that test (USB_HARDWARE_CHECKLIST.md §8).
- Verification: `tools/render_previews.py` and `tests_py/` decode the final
  296 × 128 card and full-screen QR bitmaps with zbar, at native size and
  enlarged, and require an exact payload match.
