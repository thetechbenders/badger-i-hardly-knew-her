# Hardware test records

Each file here records one test session on a physical badge: which firmware
was tested, when, by whom, how the results were obtained, and what they do
not cover. Records are evidence. They are not edited afterwards, and a
result applies only to the firmware it names, never to later builds.

| Record | Firmware | Date | Scope |
|---|---|---|---|
| [2026-10-01-usb-023b3c6.md](2026-10-01-usb-023b3c6.md) | personalised local build `023b3c6` | 2026-10-01 | USB-only checklist, owner-reported; identification and full-flash backup before the first flash |

The blank, reusable checklists are
[../USB_HARDWARE_CHECKLIST.md](../USB_HARDWARE_CHECKLIST.md) (USB power only)
and [../HARDWARE_SMOKE_TEST.md](../HARDWARE_SMOKE_TEST.md) (full procedures,
including battery and gesture sensor).

## Adding a record

1. Copy the blank checklist to `docs/test-records/<date>-<scope>-<commit>.md`.
2. Fill in the header below, then tick the steps you ran and add notes.
3. Leave untested steps unticked. Never copy ticks from an older record.

```markdown
| | |
|---|---|
| Firmware tested | commit (`> version` line), public or personalised, UF2 SHA-256 |
| Test date | YYYY-MM-DD |
| Hardware | board, power (USB / battery), sensor fitted? |
| Provenance | who ran it; observed directly or reported |
| Limitations | what was not tested, and anything specific to this badge |
```

Personal details (contacts, serial numbers, private file names) do not
belong in a public record. Describe the content generically ("4 contacts,
7 projects, a teaser without a link").
