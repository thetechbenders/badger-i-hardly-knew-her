# Controls and screens

This firmware has photo badge, contact card, full-screen contact QR, project pages, project QR, project index, diagnostics/info and recovery states. It does **not** cycle screens on an arbitrary timer; navigation is a deliberate button/gesture/CLI action.

| Button | Short press | Long press (at least 1 s) |
|---|---|---|
| **A** | Photo badge | Clean full e-paper refresh |
| **B** | Contact card | On a project: open its link QR if present; on a project QR: return to that project; otherwise: open contact QR if configured (including from badge/card), or show the card. **On a contact QR, long B does not return**; use short UP/DOWN. |
| **C** | Last project (if configured); double press: project index | Project 1 from anywhere |
| **UP / DOWN** | Previous/next project, or return from a QR | UP toggles optional gesture mode; DOWN powers off now |
| **USR** (2040) / **HOME** (2350) | Diagnostics and information | Toggle layout A/B for the session |

The project index appears with **double C**. UP/DOWN move the highlight with repeat; C opens highlighted project; A cancels and returns without changing the remembered project. The single-C action waits roughly **0.35 s** for a possible double-press. Quiet browsing batches the refresh after activity settles, rather than flashing for every repeat step. On the project index, holding DOWN for **3 s** powers off, since shorter holding scrolls.

On the **Badger 2040 Info screen**, UP/DOWN short presses additionally choose LiPo, AAA alkaline or AAA NiMH, from the merged 2×AAA battery support. The Badger 2350 does not offer this switch.

## Layouts, cards and projects

- **Photo badge:** layouts A (portrait left, default) and B (portrait right), with name, title, interests and optional event banner.
- **Business card:** typed contact rows, optional QR and caption. GitHub/Discord can use icons. With no QR configured, card width is reclaimed instead of drawing an invalid placeholder.
- **Portfolio:** up to 12 project pages, in form order, with optional status, link and teaser text. A project **without a link cannot open a project QR**.
- **Index:** lets the user select a project from up to 12 configured entries.
- **Diagnostics:** version, battery information, reset state, display and sensor status; deeper detail over USB.

## Refresh and power

E-paper flashes or briefly inverts as part of the panel's waveform. The host scheduler coalesces requests and suppresses unchanged frames; refresh timing in [REFRESH.md](../docs/REFRESH.md) is largely derived from driver waveforms, **not a guaranteed measured wall-clock duration**. Badger 2350 uses full refresh only. The last image remains visible when a battery-powered badge sleeps.

See [power and batteries](Power-and-Batteries.md), [gesture navigation](Gesture-Sensor.md), [refresh behavior](../docs/REFRESH.md), and [button/index state-machine details](../docs/ARCHITECTURE.md#buttons-c-gestures-and-the-project-index).
