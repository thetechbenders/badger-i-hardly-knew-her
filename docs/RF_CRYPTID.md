# RF Cryptid: Badger 2350 MVP

**Status:** Software implementation, host tests and firmware cross-builds; physical
Badger 2350 wireless/passive-scan and e-paper validation still required.

RF Cryptid is a specimen plate, not a Wi-Fi browser or diagnostic scanner.
The CYW43 radio is used for a finite **passive Wi-Fi scan**, with no SSID
association, internet connection, credentials, AP advertising, deauthentication,
or packet capture. The device does not store or display SSIDs or BSSIDs.

## Controls

**Badger 2350 only:** tap **USR** to open diagnostics, then **hold C**
for at least one second to enter RF Cryptid. The badge immediately surveys.
On the specimen plate, **tap C** to scan again (the normal C double-click
recognizer delays this action about 350 ms), **tap A** to return to the badge,
or **tap USR** to return to diagnostics. Existing ordinary button assignments
are unchanged outside diagnostics and this new screen. No gesture sensor is needed.

CLI: `screen cryptid` enters and surveys on 2350; rejected on 2040 and in
safe mode. No radio is initialized on the 2040. A failed scan shows an explicit
error plate, and an empty scan shows an empty-habitat plate rather than inventing
a specimen. Scans have a software timeout; the firmware polls the CYW43 on
core 0 and deinitializes it after the survey completes.

## The specimen grammar

The collection adapter holds up to 48 unique BSSIDs in volatile RAM during a
survey, prioritizing stronger observations when saturated. Metadata used:
2.4 GHz channel, RSSI, whether SSID is hidden, and open/secured classification.
The portable normalizer deduplicates BSSIDs, ignores invalid channels, bins
signal strengths into strong (>= -60 dBm), medium (>= -78 dBm) and weak,
and counts channel groups 1–5, 6–10 and 11–14.

All output is deterministic for the same normalized observation set:

| Ecological property | Morphology |
|---|---|
| AP density (coarse tiers) | Number of mantle segments, overall breadth |
| Strong-signal population | Branching filament count |
| Channel spread | Paired sensory antennae |
| Hidden SSIDs | Barbed lateral spines |
| Open network count | Ocular organ cluster |
| 2.4 GHz band imbalance | Asymmetric outer fronds |
| Coarse ecological signature | Pseudo-Latin binomial name, peripheral sigils |

The signature deliberately hashes **quantized aggregate features**, not MAC
addresses or exact RSSI. Small scan fluctuations generally preserve the
organism's recognizable anatomy, although crossing a category threshold
may change its name. Substantially different habitats tend to change several
anatomical properties. The plate's field note describes the coarse habitat;
none of these motifs are claims about radio physics.

No persistent encounters, bestiary, cloud, SSID history, or user-controlled
image generation are included.

## Host verification

Build the host renderer with `-DBHIHKH_TARGET=badger2350` and use:

```sh
mkdir -p build/rf-plates
build/host-badger2350/badger_preview --screens cryptid --rf-fixture --out build/rf-plates
```

The `--rf-fixture` argument supplies a **synthetic**, fixed survey; it is not
a radio scan. The ordinary portable host tests exercise fingerprint
deduplication, ordering, jitter tolerance, morphology, App entry/rescan/exit,
and repeatable framebuffer output. The existing Badger 2040 host and firmware
builds remain regression gates.

## Physical validation outstanding

- Verify Badger 2350 CYW43 initialization, RF front-end power and correct
  passive scan mode on the physical board; verify no active probes with
  independent observation if this guarantee is essential.
- Check callback completion, timeouts and repeated scan/exit/rescan.
- Check battery draw during and after a scan, e-paper line detail and ghosting,
  and responsiveness during dual-core display updates.
- Validate survey behavior in empty/crowded environments and on battery power.

A passive scan **is not radio silence**: enabling a station can have
device/firmware-specific management behavior. No transmit-free certification
is claimed until measured.

Potential later ideas (not implemented): a single alternate habitat sigil
page; optional privacy-preserving coarse encounter familiarity indicator.
