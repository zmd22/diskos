# diskOS UI fork — features

> **Thank you, [b0hemia](https://github.com/b0hemia)!** This work stands entirely on
> [diskOS](https://github.com/b0hemia/diskos): the custom firmware, installer and original UI. All credit for
> diskOS goes to b0hemia and the diskOS contributors; this fork only reworks the on-device interface.

> **Disclaimer:** provided **as is**, for **testing and experimentation only**, without warranty of any kind.
> Custom firmware / UI software can misbehave, lose data or make a device temporarily unusable. **Use at your own
> risk**; the author of this fork accepts no responsibility or liability. Not affiliated with FiiO or upstream diskOS.

A reworked touch interface for the **FiiO Snowsky Disc** (round 360×360 screen, firmware V2.40), built on the
UI of **diskOS 1.1.2**. Only `ui/` changes (the `mq_ui` binary); the installer is unchanged. Two complete
looks ship in the same build — **Ring** (default) and **Braun** — switchable in Settings › Display › Theme. Braun also comes as **Braun Dark** (charcoal body, aluminium knobs), and **Auto day/night** switches Braun to dark from 20:00 to 07:00.

> The screenshots are rendered from the real UI code (LVGL, no device); the track, cover, battery, weather and
> volume shown are invented test data. See `render-harness/`.

![Ring overview](screenshots/00-overview.png)

---

## Two themes

### Ring — rings and orbits
Black, made for the round screen: progress rings, arcs on the rim, menus whose buttons orbit a hub, lists
whose rows curve with the circle. The accent colour (default red, or follow the album) marks what's on;
media screens take the album's colour.

### Braun — 70s Braun / Dieter Rams
Warm off-white with a **speaker-grille** dot texture, **dark-grey knobs** with white icons, a pointer and an
indicator lamp, a solid **lower segment** panel following the round edge, straight lists, **Inter** type, and one
**orange** accent that means only "on / primary / focused". Switching restarts the UI in a couple of seconds.

![Braun overview](screenshots/braun/00-overview.png)

### Braun Dark
The same Braun in a dark finish: a charcoal body with dark grille dots, off-white type, **aluminium knobs with dark
icons** and the same orange accent. Pick it in Settings › Display › Theme, or turn on **Auto day/night** (right after
Theme) to run Braun dark from 20:00 to 07:00; the switch happens the next time the screen is off.

![Braun Dark overview](screenshots/braun-dark/00-overview.png)

| | Ring | Braun | Braun Dark |
|---|---|---|---|
| Home | ![](screenshots/01-home.png) | ![](screenshots/braun/01-home.png) | ![](screenshots/braun-dark/01-home.png) |
| Now Playing | ![](screenshots/02-now-playing.png) | ![](screenshots/braun/02-now-playing.png) | ![](screenshots/braun-dark/02-now-playing.png) |
| Volume | ![](screenshots/03-volume.png) | ![](screenshots/braun/03-volume.png) | ![](screenshots/braun-dark/03-volume.png) |
| Now Playing options | ![](screenshots/04-now-playing-options.png) | ![](screenshots/braun/04-now-playing-options.png) | ![](screenshots/braun-dark/04-now-playing-options.png) |
| Quick Settings | ![](screenshots/05-quick-settings.png) | ![](screenshots/braun/05-quick-settings.png) | ![](screenshots/braun-dark/05-quick-settings.png) |
| Settings | ![](screenshots/06-settings.png) | ![](screenshots/braun/06-settings.png) | ![](screenshots/braun-dark/06-settings.png) |
| Shortcuts | ![](screenshots/10-shortcuts.png) | ![](screenshots/braun/10-shortcuts.png) | ![](screenshots/braun-dark/10-shortcuts.png) |
| Equalizer | ![](screenshots/11-equalizer.png) | ![](screenshots/braun/11-equalizer.png) | ![](screenshots/braun-dark/11-equalizer.png) |
| Queue | ![](screenshots/12-queue.png) | ![](screenshots/braun/12-queue.png) | ![](screenshots/braun-dark/12-queue.png) |
| Standby | ![](screenshots/13-standby.png) | ![](screenshots/braun/13-standby.png) | ![](screenshots/braun-dark/13-standby.png) |
| Long-press menu | ![](screenshots/22-song-menu.png) | ![](screenshots/braun/22-song-menu.png) | ![](screenshots/braun-dark/22-song-menu.png) |

---

## Home & standby
* **Ring:** the clock inside the playing track's progress ring, the cover riding the ring, weather above the
  clock, title (album colour) and artist below; **Library · play/pause · Search** as large buttons (56 px circles
  either side of a 68 px play/pause, all sharing one top edge); the battery is an arc on the top rim.
* **Braun:** a Braun wall clock (numerals, orange second hand) with the weather in a window at 3 o'clock; the
  track and the three buttons on the lower segment (40 px circles either side of a 50 px orange play disc, tops
  aligned); a thin black battery arc.
* **Standby:** Ring = a large dimmed ring clock (weather on top, big time, title/artist); Braun = the clock
  enlarged with hour marks only, dimmed, redrawn once a minute. Five classic saver styles remain selectable.

## Now Playing
* Styles: **Ring** (cover in its progress ring — drag the ring to seek), **Poster**, **Cover**, **Vinyl**;
  in Braun its own layout (cover on a panel inside an orange ring, controls on the segment).
* **Immersive view** (tap the cover): the cover with synced lyrics.
* **Volume popup:** Ring = an accent arc on the right rim with a large number; Braun = a dark scale band with
  light marks, an orange needle and a big number box. Drag it; tap the middle to close; it hides by itself.
* **Options** (swipe left or ⋯): Favourite, Playlist, Lyrics, Equalizer, Info, Album, Artist, Tags orbiting
  the cover (an audiobook variant with Chapters and Details).
* A queue icon with a count under the play-mode icon.

## Menus
Quick Settings (configurable tiles), Settings and Working mode are **orbits** around a hub (tap the hub to go
back; **hold any back control to jump straight Home**); in Braun they are knobs with white icons whose pointer turns orange and lamp lights when active.
**Rescan** asks "Rescan library?" first (Quick Settings and Settings), and says "Already scanning" while a scan runs.

## Working modes
Pick a mode on the **Working mode** orbit: Local, USB DAC, BT DAC, BT streaming, AirPlay, USB storage. While any
mode other than Local is active it gets **its own screen**: a big icon (the same symbol as its orbit button), the
mode name, a status line, a detail line, and two buttons — **Modes** (back to the picker) and **Local** (back to
normal playback). Ring = the icon in a ring that turns while waiting and goes solid when connected; Braun = the icon on
a dark knob whose lamp lights when connected.

| Mode | Status and details | Ring | Braun |
|---|---|---|---|
| Picker | six modes around a hub | ![](screenshots/14-working-mode.png) | ![](screenshots/braun/14-working-mode.png) |
| USB storage | waiting / connected to computer, SD card size, "Eject on the computer first" | ![](screenshots/15-mode-usb-storage.png) | ![](screenshots/braun/15-mode-usb-storage.png) |
| USB DAC | waiting / connected, volume, sample rate if reported | ![](screenshots/16-mode-usb-dac.png) | ![](screenshots/braun/16-mode-usb-dac.png) |
| BT streaming | the speaker streamed to | ![](screenshots/17-mode-bt-streaming.png) | ![](screenshots/braun/17-mode-bt-streaming.png) |
| BT DAC | the connected phone or computer, volume | ![](screenshots/18-mode-bt-dac.png) | ![](screenshots/braun/18-mode-bt-dac.png) |
| AirPlay | "Pick the Disc on your device", or playing title and artist | ![](screenshots/19-mode-airplay.png) | ![](screenshots/braun/19-mode-airplay.png) |
| Waiting state | e.g. no speaker connected | ![](screenshots/20-mode-waiting.png) | ![](screenshots/braun/20-mode-waiting.png) |

## Power
* **Shut down player** is the last row of Settings › System, with a Cancel / Shut down confirmation.
* A themed **Shutting down** screen (Ring: accent ring round a power symbol; Braun: dark knob with its lamp lit)
  appears when you shut down from Settings, when auto power-off fires, and when you hold the power key (shown after
  5 seconds, while the player's own shutdown runs; hold time `pwr_hold_ms`, default 5000).
* **Auto power-off** with a 30 s countdown that a tap cancels.

| | Ring | Braun |
|---|---|---|
| Settings › System, bottom | ![](screenshots/08-settings-system-bottom.png) | ![](screenshots/braun/08-settings-system-bottom.png) | ![](screenshots/braun-dark/08-settings-system-bottom.png) |
| Shut down confirmation | ![](screenshots/09-shutdown-confirm.png) | ![](screenshots/braun/09-shutdown-confirm.png) | ![](screenshots/braun-dark/09-shutdown-confirm.png) |
| Rescan confirmation | ![](screenshots/21-rescan-confirm.png) | ![](screenshots/braun/21-rescan-confirm.png) | ![](screenshots/braun-dark/21-rescan-confirm.png) |
| Shutting down | ![](screenshots/23-shutting-down.png) | ![](screenshots/braun/23-shutting-down.png) | ![](screenshots/braun-dark/23-shutting-down.png) |

## Shortcuts (swipe left from Home)
Up to **five shortcuts** of your choice, set in **Settings › Display › Shortcuts**: Weather, Immersive,
Equalizer, Folders, Lyrics, Queue, Audiobooks, Search, Battery, Song Info, Last.fm, **All apps**, or any
installed homebrew app. All apps shows every app (Last.fm, homebrew apps, Settings), so nothing is unreachable.

## Library & browsing
* Curved lists with position dots (Ring) or straight lists with a single orange focus dot (Braun); fast with
  very large libraries (tested with 30,000 songs and 3,000-entry folders/playlists).
* Album wall, artists, albums, genres, favourites, playlists, history.
* **Long-press a song:** Add to queue, Playlist, Favourite, Album, Artist, Info, Tags.
* **Folder browser** with file operations: rename, copy/move, delete (the playing track is protected; deletes
  confirm). Renames/moves/deletes keep playlists and the queue up to date.
* **Search** with a big-key keyboard.

## Queue (up next)
Enqueue songs or folders: they play **after the current song** (or after the last queued one), in order —
Shuffle/Repeat paused — then playback **returns to what you were playing**, in your play mode. Next jumps
straight to the first queued song; Previous is left alone. The Queue screen shows playing now / up next /
where playback returns: tap to jump, drag to reorder, Shuffle or Clear. Survives a restart.

## Playlists
Playlists with song counts and an "Add to playlist" picker in the same style.

## Equalizer
Ten bands, **±6 dB in 0.1 dB steps**, **movable band frequencies** (parametric-lite, fixed Q), −/+ fine
steps, a number pad, double-tap to reset a band; built-in presets read-only. Ring = round "pizza" dial;
Braun = a **fader bank** with a lamp over the selected band. *Known gap:* the built-in presets (Jazz, Rock, ...)
show flat, because their curves are not in the player's EQ table.

## Lyrics & artwork tagging
From any song/album/folder menu, or automatically (**Auto-tag**): synced lyrics (lrclib) and 600 px covers
(iTunes) written into the file's own tags (FLAC and MP3), only where missing. The Lyrics screen shows them in
large type (20 px).

## More screens
* **Song Info** with bitrate · **Battery & usage** — a 24-hour dial of battery level, screen-on and playing,
  7 days of history · **Weather** dial (next 24 hours around the rim; tap the place to change the city, hold for
  automatic) · **Audiobooks** with progress rings and **Chapters** as a ring of segments · **Wi-Fi / Bluetooth**
  with status rings (Ring) or status knobs (Braun); the connected speaker shows its codec ("Connected - LDAC"); **Settings > Audio > BT Codec** picks LDAC Balanced (default) / Quality / Connection, AAC or SBC, with an LDAC > AAC > SBC fallback · **Last.fm** · **Accent colour** ring · toasts and a
  **library-rescan** indicator on the rim.

## Text & fonts
Montserrat (Ring), Inter (Braun) and the international fonts (CJK, Cyrillic…) chained, including typographic
punctuation (’ “ ” … – ·) from track titles and lyrics.

## Reliability & performance
SD-card safety (periodic filesystem flush), stability fixes (use-after-free in long lists, parser bounds
checks), time-limited **Bluetooth checks** (a stuck Bluetooth service can no longer hold the screen for seconds, and turning Bluetooth on from Quick Settings now starts audio routing), the **brightness you set survives a power-button screen off/on**, Wi-Fi/Bluetooth
commands with timeouts, heavy work off the UI thread, lists that only build visible rows. Checked with sanitizers
and static analysis during development.

## Install
```bash
./diskos-installer install --firmware SNOWSKY_DISC_update_V2.40.zip --ui /path/to/mq_ui
```
Quick preview without flashing: copy `mq_ui` to the player over SSH (see `docs/PREVIEW_UI_BUILD.md`).
Build from source: clone b0hemia/diskos at `v1.1.2`, `git apply diskos-ui-fork-vs-upstream.patch`, then
`cd ui && make CROSS=mipsel-linux-musl-` (musl cross toolchain; see DEVELOPMENT.md). `mq_ui` md5:
`1006661e90d9d64054ac84dcbfac8fd2`.

See **CHANGES.md** for the detailed change log.
