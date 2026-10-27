# Privacy and reproducible backups

A **personalized badge UF2 is not anonymous**. It can embed your name, contact data, portfolio links, QR payload and processed portrait in firmware or assets. Even previews, TOML/JSON settings exports and flash dumps can carry the same material.

## Keep private inputs local

The repository ignores `local/`. Appropriate locations:

- `local/badge.toml` and your private source photo.
- `local/profile.json`, `local/portrait.png` and `local/out/` generated personalized artifacts.
- `local/studio/` for the Studio workspace and managed processed portraits.
- `local/backups/` for private, reproducible archives and device backups.

Do **not** commit any of the above, put them in a PR/wiki/release asset, upload complete Studio launch URLs, or include unredacted contacts in screenshots. A Studio bearer token authorizes local workspace API access; a fresh one is printed by each server instance. Loopback, Host/Origin checks and the token reduce exposure, but are **not** an operating-system malware defense.

## A reproducible restore path (Badger 2040 only)

**Important:** `scripts/private-backup.sh` is currently **Badger 2040-only**. It hardcodes the `badger_badge.uf2` artifact and uses the default 2040 build/verification target. **Do not use its archive as a Badger 2350 firmware backup or restore procedure.** On a 2350, retain private inputs and a board-specific flash/firmware backup using the [installation instructions](Installation-and-Recovery.md); no equivalent automated personalized archive verification is implemented yet.

For a Badger 2040, the repo includes this private backup workflow:

```bash
scripts/private-backup.sh create local/badge.toml
scripts/private-backup.sh verify <archive>
```

Follow [PERSONALIZE.md's private backup section](../docs/PERSONALIZE.md#private-backup) for prerequisites and limitations. The archive can include inputs, personalized firmware made from a clean commit, a Git bundle of source, checksums and a restore guide. The verification process extracts to a temporary directory, rebuilds and compares the output against the saved firmware. Keep this archive secured, encrypted at rest if practical, and separate from public source control.

**Stock-device backup is a different operation.** `picotool save -a` captures a raw flash image for restoration of BadgerOS/BadgeWare, but its contents may contain sensitive personal information and are board-specific. See [installation](Installation-and-Recovery.md).

## Privacy-aware validation

Public CI uses fictional sample inputs. It does not have access to your local personalized image or saved TOML. Host checks can validate public sample rendering but cannot certify your own QR readability or physical badge output. Document the tested build and limits without uploading secrets.

For contributing/redaction rules see [Contributing](Contributing.md) and [test records](../docs/test-records/README.md).
