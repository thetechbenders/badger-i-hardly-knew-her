# Personalizing your badge

Your identity, contact links and portrait are **inputs**, not source-code changes. Keep them beneath the Git-ignored `local/` directory. The committed `config/badge-form.toml` and `config/sample-profile.json` are fictional examples, not places to put real contacts.

## Fill-in TOML workflow (recommended for a complete firmware image)

```bash
scripts/build-badge.sh --new
$EDITOR local/badge.toml
# Place a JPEG/PNG under local/ and set [portrait] photo = "photo.jpg".
scripts/build-badge.sh --check local/badge.toml
scripts/build-badge.sh --preview local/badge.toml
scripts/build-badge.sh local/badge.toml
```

For the **2350**, add `--target badger2350` to the preview/build invocations. The form builder uses the correct taller **104×176** portrait crop for that target. Unlike the form pipeline, [Studio](Studio.md) intentionally retains a **104×128** workshop composition for both targets.

## Common fields

| TOML section | Purpose |
|---|---|
| `[person]` | Name, title, affiliation, event label, interests |
| `[[contacts]]` | Up to six typed contact lines; GitHub/Discord can show icons |
| `[qr]` | No QR, HTTPS link, vCard, or vCard derived from contacts |
| `[portrait]` | Original photo or processed portrait; crop, method and tone controls |
| `[[projects]]` | Up to twelve projects with title, tagline, description, status, optional teaser/banner and link |
| `[preferences]` | Layout, refresh, sleep and other supported settings |

The form validator checks lengths, supported glyphs, email/phone/link types, required fields, limits and safe file paths. Rendering checks text fit across all configured screens and decodes QR pixels to confirm they match the intended content. A failing check stops the build rather than silently omitting text or making a fake QR.

## Portrait choices

The conversion pipeline crops, scales, adjusts tonal levels and converts to one bit. Available methods are threshold, Bayer 8×8, Floyd–Steinberg and Atkinson. Review the generated `portrait-methods_x3.png` and `previews/contact_sheet.png` before flashing. A processed portrait may also be deployed in a dedicated asset UF2 rather than embedded into the firmware.

## Existing JSON profile or USB settings

`local/profile.json` remains supported. The form export utility can migrate an existing profile to TOML; consult [PERSONALIZE.md](../docs/PERSONALIZE.md#already-have-a-localprofilejson) for the exact command. A profile built into firmware supplies **defaults**; previously committed USB settings take priority until you explicitly reset them.

## Safety and privacy

Your form, original photo, processed picture, previews and personalized firmware can all carry private information. Never add them to a PR, issue, screenshot, public GitHub Wiki or release. Use [private backups](Privacy-and-Backups.md) for reproducibility.

Sources: [form walkthrough](../docs/PERSONALIZE.md), [annotated template](../config/badge-form.toml), [portrait/QR design](../docs/DESIGN.md), [USB keys](../docs/USB_CLI.md).
