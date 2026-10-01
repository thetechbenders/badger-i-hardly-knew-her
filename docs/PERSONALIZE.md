# Personalising your badge

You fill in one commented text file, add your photo, and run one command.
No C++ to edit, no JSON to write. The form is
[`config/badge-form.toml`](../config/badge-form.toml); every field is
explained in it, next to the field.

## Walkthrough

```bash
scripts/build-badge.sh --new                       # 1. copy the template -> local/badge.toml
$EDITOR local/badge.toml                           # 2. fill in the fields
cp ~/Pictures/me.jpg local/photo.jpg               # 3. supply your photo; in the form:
                                                   #      [portrait] photo = "photo.jpg"
scripts/build-badge.sh --preview local/badge.toml  # 4. look at every screen, nothing is flashed
scripts/build-badge.sh local/badge.toml            # 5. build -> local/out/badge/fw/badger_badge.uf2
```

Then flash as in [INSTALL.md](INSTALL.md): hold **BOOT/USR**, tap **RST**,
and copy `local/out/badge/fw/badger_badge.uf2` onto the `RPI-RP2` drive.
Read INSTALL.md first if the badge still runs BadgerOS.

`local/` is ignored by Git, so the form, the photo and everything made from
them stay on your computer. The template in `config/` holds only generic
example content; don't put your details there.

## What the command does

`scripts/build-badge.sh <form>` runs `tools/badge_form.py build`. Every step
must pass before the next one starts:

1. **Validate the form.** Unknown or misspelt fields ("did you mean
   `description`?"), wrong types, unsupported choices, more than 6 contacts or
   12 projects, non-`https://` links, malformed e-mail addresses, phone
   numbers and GitHub/Discord user names, characters the badge font cannot
   draw, line breaks in one-line fields, byte limits, and missing or
   unsuitable portrait files. All problems are listed at once, each naming
   its field, for example:

   ```text
   local/badge.toml: 2 problem(s) to fix:
     - projects #3 (Weather Station).link: 'http://example.com' must be an https:// address without spaces
     - person.name: 48 bytes, the limit is 47: shorten it by 1 bytes
   ```

2. **Prepare the portrait** (`local/out/badge/portrait.png`) with
   `tools/portrait.py`: crop, scale in linear light, global levels, gamma,
   mild sharpening, then 1-bit conversion. The original photo is only read.
   `portrait-methods_x3.png` shows the four conversion methods side by side.
3. **Generate the profile** (`local/out/badge/profile.json`). It is the same
   profile model a hand-written JSON profile uses, validated by
   `tools/badge_profile.py` with the firmware's limits. Never edit it; edit
   the form. If a `profile.json` that the tool did not write is already in
   the output directory, the tool stops rather than overwrite it.
   `local/profile.json` is never touched.
4. **Check that everything shows.** The firmware renderer (host build)
   reports whether every supplied field was drawn completely: on the photo
   badge in both layouts (USR switches between them), the card, the
   full-screen QR, every project page, its repository QR page and its row
   in the project index. Anything ellipsized or left out stops the build
   and names the field. Text is never cut silently. The one deliberate
   exception is reported as a note: with many contact lines, the card has
   no room for the QR caption, which then appears only on the full-screen
   QR.
5. **Render the previews** (`local/out/badge/previews/`) and decode every
   QR code from the final bitmaps with zbar. Each must read back exactly
   the configured link or vCard. A code that cannot be drawn stops the
   build; no placeholder code is ever drawn.
6. **Cross-compile** the RP2040 firmware into `local/out/badge/fw/`, then run
   `scripts/verify_artifacts.py` on it (UF2 regions, linker placement,
   metadata, checksums).

`--check` stops after step 1 (it needs no compiler), and `--preview` after
step 5.

## Previewing before flashing

`local/out/badge/previews/contact_sheet.png` shows every screen at 3× in one
image. Single screens are in `previews/x3/` (enlarged) and
`previews/native/` (exact 296 × 128 panel pixels). `qr_report.json` holds
the decode results. Adjust the form and run `--preview` again until you
are happy; it takes about a second once the host tools are built.

For the portrait: start without any tone settings. If the face is too dark
or too light, change `gamma` (below 1 lightens). If the framing is off, set
`crop = [x, y, width, height]` in photo pixels, keeping width:height at
13:16. Compare `portrait-methods_x3.png` to choose a `method`.

## Your content, your choices

Nothing on the badge is fixed except the BHIHKH! firmware itself. The
sample and template use a fictional person ("Alex Example") and
`example.com` destinations; replace all of it.

- **Portrait.** Any head-and-shoulders photo works; see
  [DESIGN.md](DESIGN.md#portrait-conversion) for how the crop, gamma and
  method change the result. No photo yet? Leave the sample silhouette
  (`processed = "../assets/sample/portrait_placeholder.png"`): the build
  notes it.
- **Event label.** `[person] event` is the strip along the bottom of the
  photo badge (and the kicker in layout B), e.g. the conference you are
  attending, your company, or nothing at all. Leave it empty to drop the
  strip; change it per event and rebuild, or over USB with
  `> set event Example Expo 2027` and `> commit`.
- **Portfolio.** 0 to 12 projects, in your order. None are required: no
  project has to come first or last, a teaser (title plus `banner`, no
  `link`) is optional, and an empty portfolio is valid. See
  [DESIGN.md](DESIGN.md#your-portfolio) for suggestions on crediting
  co-authors and upstream projects.
- **QR code.** A link, a vCard, a vCard built from your contacts, or none.
- **Layout and timing.** `[preferences]`: portrait left or right, refresh
  speed, battery sleep timeout.

## Fields at a glance

| Section | Fields | Notes |
|---|---|---|
| `[person]` | `name`*, `title`*, `affiliation`, `event`, `interests` | `interests` is text or a list joined with " · " |
| `[[contacts]]` (≤ 6) | `type`*, `label`, `value`* | type: `email`, `phone`, `web`, `github`, `discord`, `text`; GitHub/Discord show an icon instead of the label |
| `[qr]` | `show`*, `link`, `vcard` / `vcard_file`, `caption` | show: `none`, `link`, `vcard`, `vcard-from-contacts` |
| `[portrait]` | `photo` or `processed`*, then `crop`, `method`, `gamma`, `black_pct`, `white_pct`, `sharpen` or `settings` | `settings` takes an existing `tools/portrait.py` JSON |
| `[[projects]]` (≤ 12) | `title`*, `tagline`, `description`, `status`, `banner`, `link` | in form order; no `link` = no QR and no footer |
| `[preferences]` | any numeric/boolean key from [USB_CLI.md](USB_CLI.md) | e.g. `layout`, `refresh.speed`, `sleep.timeout_s` |

\* required. An optional field left empty (`""`) or deleted disappears
cleanly. A `[[contacts]]` or `[[projects]]` block with every field empty is
left out, with a note.

Text: the byte limits are written next to each field in the template.
`"""triple quotes"""` allow line breaks in `interests`, `tagline` and
`description`; spaces at the start and end are removed. The badge draws
Latin letters including accents (é, ñ, ø, ß), digits, punctuation and
– — ‘ ’ “ ” • … € →. Anything else is refused.

## Windows

Build in **WSL2** (Ubuntu 24.04), as described in [BUILD.md](BUILD.md):

- Keep the clone in the Linux file system (`~/src/badger-i-hardly-knew-her`).
  Open the form from Windows at
  `\\wsl$\Ubuntu-24.04\home\<you>\src\badger-i-hardly-knew-her\local\badge.toml`.
  Notepad and VS Code both work; Windows line endings and Notepad's UTF-8
  marker are accepted.
- Copy your photo into `local/` the same way, or point the form at it in
  Windows: `photo = 'C:\Users\you\Pictures\me.jpg'` (single quotes). Under
  WSL this is read as `/mnt/c/Users/you/Pictures/me.jpg`. A photo outside
  `local/` is not included in a private backup.
- Look at the previews from Explorer (`...\local\out\badge\previews\`).
- Copy the firmware to Windows for flashing:
  `cp local/out/badge/fw/badger_badge.uf2 /mnt/c/Users/<you>/Downloads/`,
  then drag it onto the `RPI-RP2` drive.

## Already have a `local/profile.json`?

It keeps working: CMake, `badgerctl.py push` and `scripts/private-backup.sh`
behave as before. To move to the form without retyping anything:

```bash
python3 tools/badge_form.py export local/profile.json --form local/badge.toml \
    --processed local/portrait.png        # or: --photo local/private/<photo> --settings local/private/portrait.json
scripts/build-badge.sh --preview local/badge.toml
```

`export` never overwrites a file. The form it writes gives exactly the same
profile as the JSON, and therefore the same screens and the same firmware.
Untyped contacts become `type = "text"`, which is drawn the same way. A QR
caption without a QR code (never shown) is kept as a comment, because the
form refuses a caption with nothing to describe. Your
JSON and portrait are left as they were.

`badgerctl.py push local/badge.toml` pushes the form's profile over USB
like a JSON profile (contacts, QR, projects and preferences; the portrait
is built into the firmware).

## Private backup

`scripts/private-backup.sh create local/badge.toml` builds from the form
(with all of the checks above) and archives the form, the files it refers
to, the generated profile and portrait, the firmware, a source bundle and
build information. Every file the form refers to must be inside `local/`.
`scripts/private-backup.sh verify <archive>` restores into a temporary
directory, regenerates the profile and portrait from the form, checks they
are identical to the archived ones, rebuilds and compares the firmware byte
for byte. Without an argument, `create` keeps using `local/profile.json` and
`local/portrait.png` when both exist, as before. Existing archives are
never replaced: a second backup of the same commit gets a `-2` suffix.
