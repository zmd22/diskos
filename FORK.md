# diskOS — reworked UI (fork)

> **Thank you, [b0hemia](https://github.com/b0hemia)!** This work stands entirely on
> [diskOS](https://github.com/b0hemia/diskos) — the custom firmware, installer and original UI that made any of
> this possible. All credit for diskOS itself goes to b0hemia and the diskOS contributors; this fork only
> reworks the on-device interface.

> **Disclaimer:** this software is provided **as is**, for **testing and experimentation only**, without warranty
> of any kind, express or implied. Installing custom firmware or UI software can misbehave, lose data or make a
> device temporarily unusable. **You use it entirely at your own risk**; the author of this fork accepts no
> responsibility or liability for any damage, data loss or other consequences arising from its use. This fork is
> not affiliated with or endorsed by FiiO or the upstream diskOS project. (See also the warranty disclaimer in
> the GPL, `ui/COPYING`.)

This fork of [b0hemia/diskos](https://github.com/b0hemia/diskos) by [zmd22](https://github.com/zmd22) replaces the on-device UI (`ui/`) with a
reworked interface for the round screen of the FiiO Snowsky Disc: rings and orbits, a Ring clock home,
an up-next queue, a parametric-lite equalizer, lyrics & artwork tagging and much more.

* **Based on:** diskOS **1.1.2** (`ui/` unchanged from 1.1.0 to 1.1.2). Upstream 1.1.3's UI changes are not included.
* **Features, with screenshots:** [docs/fork/FEATURES.md](docs/fork/FEATURES.md)
* **Detailed change log:** [docs/fork/CHANGES.md](docs/fork/CHANGES.md)

## Install
Download `mq_ui` from this repository's Releases and install it with the regular diskOS installer:

```bash
./diskos-installer install --firmware SNOWSKY_DISC_update_V2.40.zip --ui /path/to/mq_ui
```

Tested on firmware **V2.40**.

## Build
Same as upstream: `cd ui && make CROSS=mipsel-linux-musl-` with the mipsel musl cross toolchain.

## Licence
The UI keeps diskOS's licensing (GPL-3.0-or-later for `ui/`, see `ui/COPYING`); new files carry
SPDX headers.
