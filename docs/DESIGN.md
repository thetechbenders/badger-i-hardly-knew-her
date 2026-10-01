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
`layout 1`, and a long press on USR swaps layouts for the session, so both
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
tagline/status/banner/link) are skipped entirely: no labels, rules or gaps are left behind.
A card with no contacts shows only the identity block. Project taglines
that do not fit one line wrap onto two lines of bold 10 instead of being cut.

`scripts/run-host-tests.sh` also writes `build/previews/diagnostic-max/`, a
**HOST DIAGNOSTIC SAMPLE**. Every text field is filled to its byte limit with
labelled filler, with LOW battery, a gesture fault and fault-state
diagnostics. It checks layout limits; it is not content.

## Portrait conversion

`tools/portrait.py` (used by the fill-in form for `photo = "..."`) turns an
ordinary photo into the 1-bit portrait. The original file is only read.
The pipeline: crop at the target's 104:128 aspect, downscale in linear
light (so thin dark lines such as glasses frames keep their weight), apply
a mild unsharp mask, apply global levels (`black_pct` / `white_pct` percent
clipped), apply `gamma`, then convert to 1 bit. Only global tone and
geometric operations are used; nothing is painted, retouched or
synthesised, and a crop that would stretch the face or enlarge the photo
is refused.

Choosing settings for your photo, with `portrait-methods_x3.png` and the
badge previews open ([PERSONALIZE.md](PERSONALIZE.md)):

- **Crop.** Head and shoulders with a little margin above the hair reads
  best at 104 × 128. A closer crop pushes the hair against the top edge; a
  wider one leaves fewer pixels for the eyes. The default is the largest
  centred area; set `crop = [x, y, width, height]` to move it.
- **Gamma.** Below 1 lightens mid-tones (hair and shadowed skin lose
  weight), above 1 darkens them. Light backgrounds usually need about
  0.6–0.8; go lower only while the face keeps its shape.
- **Levels.** A larger `white_pct` turns more of a light background pure
  white; `black_pct` does the same for the darkest pixels.

| Method | Typical result on a headshot |
|---|---|
| Otsu threshold | Strong outlines, but dark hair becomes a solid shape and the face flattens. |
| Bayer 8×8 ordered | Visible cross-hatch over face and hair; thin lines break up. |
| Floyd–Steinberg | Most tonal detail, but worm-like texture and dense speckle at 1:1. |
| **Atkinson** (default) | Clean highlights, light stipple on skin, hair keeps some structure, eyes and mouth stay distinct. |

Your portrait and its settings are private configuration: keep the photo,
any `portrait.json` and the generated portrait in `local/` (git-ignored).

## Contact icons

Typed `github` and `discord` contact lines show a 12 × 12 monochrome icon in
the label column instead of the words "GitHub"/"Discord", so the line is
only the username. The icons come from Simple Icons (CC0, see
LICENSES.md). `tools/iconsgen.py` rasterises them, with no reshaping, using a
small SVG path parser and a supersampled non-zero fill. Its `--check` mode
keeps `firmware/generated/icons.cpp` reproducible.

- The icon sits at the label column's left edge, 1 px below the line top, so
  it is centred on the value's cap height. Values keep one shared left edge
  and the 14 px line pitch. A host test checks the exact bitmap position.
- Other lines keep their bold text label. The type is explicit: a line
  labelled "GitHub" with no `type` gets no icon, so older profiles render as
  before.
- At 12 px the GitHub mark reduces to its round silhouette with the
  Octocat's tail at the lower left, and Discord's to the rounded "controller"
  face with two eyes. Both read as their platforms on the native-resolution
  previews. Checks on a physical panel are recorded, per firmware build, in
  [test-records/](test-records/README.md).

## Project portfolio

C opens the portfolio at the project viewed last; UP/DOWN browse (wrapping);
long B on a project with a `link` shows its repository QR, and long B or
UP/DOWN there returns to that same project. Each page has:

- a header with `PROJECT n/N`, left of the reserved status area;
- the title (bold, auto-sized);
- an optional teaser **banner**, white on a black band, e.g.
  "TOP SECRET - COMING SOON" on a project that deliberately has no link and
  no QR (the sample's "Secret Project");
- the tagline, always bold 10 so the hierarchy does not shift while
  browsing, up to 2 lines;
- an optional outlined **status** tag: a readiness note or the fork's
  upstream, never a version number (versions go stale on a badge that is
  not reflashed);
- the description in 11 px, falling back to 10 px before anything is cut;
- a footer with a compact label (`owner/repo` for GitHub links), plus a
  small QR glyph and "hold B" when a QR is available. Pages without a link
  have no footer at all.

The QR page re-encodes the project's own full `https://` link on every
render, so it can never show a previous project's code. Next to it, the
page shows the same compact `owner/repo` label, wrapped after `/`.

**Everything configured must fit.** `tools/render_previews.py` asks the
renderer (`badger_preview --fit`) whether each title, tagline, status,
banner, description and link label of every configured project was drawn
completely, on its page, beside its repository QR and in the index, and
whether the name, title, affiliation, interests, event, contact lines and QR
caption were drawn completely on the badge (both layouts), the card and the
full-screen QR. It fails if any was ellipsized or dropped, if text contains
characters the fonts lack, or if a one-line field contains a line break.
The one exception is by design: the card's QR caption gives way to contact
lines and is then shown only on the full-screen QR (reported as a note).
The fill-in form (`tools/badge_form.py`, [PERSONALIZE.md](PERSONALIZE.md))
runs the same gate and names the form field to shorten. A host test applies
the check to the sample portfolio and requires 11 px descriptions.
Only the labelled HOST DIAGNOSTIC SAMPLE is allowed to overflow, because
that is what it tests. With a status tag, about 80 characters of
description fit at 11 px.

### Your portfolio

The portfolio is your own configuration: any 0–12 projects, in the order
you list them (none are required, and the sample's entries are fictional).
Suggestions for a public badge:

- Describe only what the linked page itself supports, and credit
  co-authors, sponsors and upstream projects in the description.
- For a fork, a status such as "Fork of owner/project" says whose project it
  is; link the fork when the portfolio shows your own changes there.
- Use a status for readiness ("Work in progress"), not a version number:
  versions go stale on a badge that is not reflashed.
- Check that every link resolves before an event. Every project QR is
  decoded from the rendered screen and compared with its link on each
  build, but the build cannot know whether the page exists.

The font generator measures advances in FreeType's monochrome mode
(`mode="1"`), matching the 1-bit glyphs it stores. Antialiased advances had
left visible gaps inside words ("Instrum entation").

## Project index

Double C opens a list of the project names. It is a way to jump to a
project without paging through the others.

| Sample portfolio (fictional), 3/7 highlighted | 12 entries (labelled placeholders), scrolled to 10/12 |
|---|---|
| ![index](previews/x3/index_3.png) | ![index-12](previews/x3/index-12_10.png) |

Native size: [`index_3.png`](previews/native/index_3.png),
[`index-12_10.png`](previews/native/index-12_10.png); every highlight of the
sample is in `previews/*/index_N.png`, and the 12-entry example's first,
7th, 8th (first scroll), 10th and last in `index-12_N.png`.

- **Rows.** Seven rows of 14 px, each with a right-aligned number and the
  name in regular 11 px (the portfolio body size). The highlighted row is
  white on a black bar across the list width. A bar reads as "selected" on
  1-bit e-paper better than a cursor glyph or a box. It costs one row of
  black pixels, which stays within a partial refresh.
- **Header.** `PROJECT INDEX` and `n/N` in bold 10, left of the reserved
  status area, which is never drawn into (host test
  `render_status_area_reserved_on_every_screen` covers every screen).
- **Scrolling.** Up to seven entries fit without scrolling, so the
  seven-entry sample never scrolls. With more, a 3 px scrollbar appears next
  to the UP/DOWN triangles (the same triangles as on the project pages,
  beside the physical buttons). The window moves only when the highlight
  would leave it.
- **Hints.** One line: `C open · A back · hold UP/DOWN to scroll`, shortened
  for one entry (`C open · A back`) and for none (`A back`, with "No
  projects configured" in the middle).
- **Order.** The configured order. A teaser (the sample's "Secret
  Project") is listed by name only; opening it shows its banner page. Names
  never ellipsize in the sample (host test `render_index_sample_portfolio`);
  for any profile, the fit gate refuses a name too long for its row, so it
  is shortened before the build rather than cut on the badge.
- The 12-entry previews come from `render_previews.py --example-projects 12`.
  It inserts "Example project k" placeholders before the last entry. These
  are host previews of the layout, never content.

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
- Which to use is your choice (`[qr] show` in the form). Compare both on
  your phones before an event (USB_HARDWARE_CHECKLIST.md §8).
- Verification: `tools/render_previews.py` and `tests_py/` decode the final
  296 × 128 card and full-screen QR bitmaps with zbar, at native size and
  enlarged, and require an exact payload match.
