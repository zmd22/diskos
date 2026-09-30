# diskOS UI fork — changes vs b0hemia/diskos

**Base:** `b0hemia/diskos` main @ `646212d` (v1.1.2). The UI sources are identical to v1.1.0, so the patch applies to any 1.1.x.
**Patch:** `diskos-ui-fork-vs-upstream.patch`, sources only (`ui/*.c`, `ui/*.h`, plus `ui/Makefile` for the new `radial.c`).
**Binary:** `mq_ui`, md5 `fbfc2566e6c5424967f66536878a8652`.

Applying the patch to a fresh clone of upstream main and building with the project's pinned musl toolchain reproduces this exact binary, byte for byte.

```bash
git clone https://github.com/b0hemia/diskos.git && cd diskos
git apply diskos-ui-fork-vs-upstream.patch
# build ui/ as described in docs/PREVIEW_UI_BUILD.md, or flash the prebuilt binary:
./diskos-installer install --firmware SNOWSKY_DISC_update_V2.40.zip --ui mq_ui
```

---

## Fixes

### microSD wiped shortly after boot (V2.40, card inserted at power-on)

**Symptom.** Files copied to the card play fine. After a reboot with the card left in, the card reads as empty within seconds, on the player and on a PC. A card inserted *after* boot is unaffected.

**Cause.** On V2.40 the UI's cold-boot code handles a card that is already inserted in two ways at once. It re-emits the old V2.09/V2.28 "card added" uevent *and* mounts the card directly. Two parties then act on one card at boot. A card inserted later is handled only by the player, through a genuine kernel insertion event, which is why hot-inserting was always safe. This affects every 1.1.x release.

**Fix.** On V2.40 the UI never mounts the card. At cold boot it makes the kernel announce the card again by unbinding and rebinding the SD controller, which produces the same remove/add events as physically re-inserting it. The player then mounts the card exactly as in the safe hot-insert case.
* **Controller lookup:** the controller is found through the card's own sysfs path (`/sys/block/mmcblk0/device` → controller), so the SDIO Wi-Fi host is never touched.
* **Fallback:** if the player hasn't mounted the card 20 s after the re-announce, the UI leaves it alone. The library is then empty until the card is re-inserted, rather than risking a mount behind the player's back.
* **Unchanged:** the existing cold-boot window, host-export checks and one-shot behaviour are kept.
* **Status:** confirmed on hardware; a card left in across reboots survives.

**Also added:** a background flush of the card's filesystem every 4 s. The exFAT driver's default `delayed_meta` keeps directory entries and the allocation bitmap in RAM, and nothing on the device unmounts the card at shutdown, so this bounds what a hard power-off can lose.

### "Launch failed" for homebrew apps on a loaded player

`app_run()` started apps with `fork()`, which must duplicate the whole UI process. On a player holding a large queue there isn't enough free memory for that. The UI now uses `posix_spawn()`, which hands straight over to the new program without the copy.

### Work-mode table (`0657`) for V2.40

Two modes were added to `ui_set_source_mode()`, using values verified on hardware:

| Frame | Mode |
|---|---|
| `0657000C0007` | Bluetooth streaming |
| `0657000C000A` | AirPlay receiver (RAOP listener on port 5000) |

This also corrects `docs/COMMAND_MAP.md`: `0657000C0008` is plain **local playback**, not the network-receiver mode. The full table was posted separately.

---

## UI changes

### Layout refinements
* **Standby (Ring):**
  * **Ring:** bigger, radius 148 px, with a thicker line and a larger cover dot.
  * **Type:** larger all round. The weather (icon and temperature only) is on top at 28 px, then the time (48 px scaled to about 62), then the title (28 px) and artist (24 px).
  * **Removed:** the date and the weather condition.
* **Home:** the weather (icon and temperature) sits above the clock, and the title (24 px) and artist (20 px) move up into the space the date line left. The date and condition are gone. Library, play/pause and Search are larger (48 px buttons, 20/26 px icons).
* **Radial menus:** every orbit's buttons sit further out (radius 106 → 116 px). They still end inside the rim-scroll band's boundary, and the leftmost button reaches into the back-swipe start zone exactly as before, so edge gestures are unchanged.
* **Immersive:** no title or artist, just the cover and lyrics. The shading behind the lyrics is a little lighter (darkest point 240 → 205 of 255).
* **Fonts:** the non-Latin font helper now also provides 24 and 28 px.

### Themed remakes (rings, orbits, curved lists)
* **Toasts:** a dark pill near the top rim with an icon: the accent by default, a green tick for success. It's one line, sized to stay inside the circle, and dismisses itself after 2.4 s.
* **Library rescan:** while a scan runs, a small dot with a fading trail orbits the rim over whatever is on screen (20 frames a second, redrawing only those few pixels). When it finishes: "Updated · N songs" with a green tick.
* **Song Info:** the playing track's cover inside its progress ring, title and artist under it, then one-line rows (Album, Format, Sample Rate, Bitrate, Duration, File) fitted to the circle. Another song's details (from a long-press menu) show a plain disc instead of the playing cover. Audiobooks relabel as before.
* **Playlists:**
  * **Look:** the Queue is pinned first with a list icon, and each playlist shows "N songs".
  * **The picker:** the same, as curved rows with a + on each, and "New playlist…" first.
  * **Reserved name:** "Queue" can't be used as a new playlist's name.
  * **Fixes:** adding to the Queue from the picker didn't trigger the rebuild while the queue played; it does now. Adding a folder to any other playlist no longer restarts the Queue.
* **Apps:** an orbit of up to six. With more, the middle turns pages (1/2, 2/2 …), and long names end in "…".
* **Accent colour:**
  * **The ring:** ten preset colours, the current one outlined and shown large in the middle with its name.
  * **Follow album:** takes the colour from each album.
  * **Custom:** opens the full hue, saturation and brightness sliders, so any colour is still possible.
  * **Leaving:** tap the middle to go back.
* **Weather:**
  * **The dial:** the next 24 hours every 3 hours around the rim (icon, temperature, hour), with the current temperature, condition, place and today's high/low in the middle.
  * **The forecast:** fetched from wttr.in only while this screen is open, at most every 10 minutes, in its own thread. Slots are chosen by the player's clock.
  * **Location:** tap the place to type one; hold it to switch back to automatic.
  * **Tested:** the parser on full, Fahrenheit, truncated, garbage and empty replies.
* **Audiobooks:** each cover sits in a ring showing how far you've listened (green when finished), with the time left, in curved rows. The book length now comes from the library.
* **Chapters:**
  * **The ring:** one segment per chapter around the cover. Heard chapters are light, the current one is in the accent, the rest are dark. Tap a segment to jump.
  * **More than 40 chapters:** a continuous progress ring instead.
  * **Every book:** previous and next buttons (previous goes to the start of the current chapter first, like a player), and a List button with every chapter.
* **Wi-Fi:**
  * **The status ring:** grey when off, a spinning arc while turning on, and filling with signal strength once connected. The network name and IP sit under it, the switch on the right, rescan on the left.
  * **The list:** curved network rows with the lock and signal bars.
  * **Fix:** Wi-Fi commands now give up after 4 s instead of waiting forever, so a hung wpa_supplicant can't freeze the UI.
* **Bluetooth:** the same design. The ring is grey when off, a spinning arc while turning on, and full when on, with the connected device under it. Device rows are curved, reading "Connected" or "Tap to connect". The radio, scan and connect logic is unchanged.
* **Fix found by the tests:** Wi-Fi and Bluetooth rows kept their network or device in a slot the curved-list helper also uses, so curving the rows overwrote it. Tapping a row could then act on garbage, and clearing the list crashed. That data now lives in the rows' own handlers.
* **Tested:** a real-code test for each screen, 9 programs. They cover the rescan dot's orbit, Apps paging, the Accent flows, each Wi-Fi and Bluetooth state and their row data, the book rings, and the chapter jump, previous/next, list and 60-chapter cases. Also the picker's Queue notifications and reserved name, the Playlists pinning, and Song Info. All pass, and all run clean under the sanitizers.

### The Queue (up next)
Enqueued songs play right after the current song, or after the last enqueued one. They play in the order enqueued, with Shuffle and Repeat paused, and then playback returns to what you were playing, in your own play mode.
* **How:** enqueuing only edits the queue, and the song that's playing is never touched. When it ends (or you press Next), the player moves to its own next song. At that moment the UI hands it a copy: the queued songs, then that next song and the rest of your list. The first queued song starts from its beginning, with no mid-song reload and no seeking. The first version of this build did reload mid-song and could jump into the queued song at the playing song's position; that's fixed.
* **Next** with songs queued goes straight to the first of them. The player never picks its own next song, which could be a random one in Shuffle.
* **Previous** is left alone: going back never triggers the queue. The on-screen button tells the queue directly. Outside Shuffle the list order also shows the direction, and returning to the song heard before counts as back too. Forward detection works in Shuffle.
* **Songs not in the library yet** (a folder file the scanner hasn't indexed) are skipped with a notice when their turn comes, instead of blocking the queue.
* **As songs play:** each queued song drops off the queue when it starts. When playback reaches the first song of the original list, the UI switches back to that list at that song.
* **Starting something else:** the queue stays and plays once the newly started song ends. Books play alone; the queue waits for music.
* **Duplicates:** the same song may be queued twice. The player's list holds each song only once, so a repeat replays when its turn comes. A queued song that's also later in the album plays at its queued spot, and again in the album after the hand-back.
* **The Queue screen** (from the icon on Now Playing):
  * **Header:** a back arrow and "Queue", with "N up next · M min" under it.
  * **Buttons:** Play (jump to the first queued song), Shuffle (icon only; mixes the queue once) and Clear (tap twice; playback returns to your list at the same point).
  * **The list:** "Playing now", then "Up next". Tap a queued song to jump to it, skipping the ones before it; hold its handle and drag to reorder. The last line is "then back to …", and position dots run down the rim.
* **Storage:** the queue is kept in `/usr/data/diskos_queue.tsv`, so it survives a restart.
* **The old "Queue" playlist** from the previous build is no longer special. If you have one, it's now a normal playlist you can rename or delete.
* **File operations:** renaming, moving or deleting a file or folder updates the queue and every playlist in the same step.
* **Folder adds:** they keep their file order and never include audiobooks.
* **Tested:** 19 checks against a real database. They cover the copy order (A, D, E, then B, C), the jump back, songs dropping off, enqueuing during the queue, the hand-back to the album at B, Clear, duplicates and their replay, a queued song that's also later in the album, reordering, jumping, the end-of-song wait, file moves and deletes, a restart, and audiobook exclusion.

### Long-press menus (Library and folders)
The orbit style, with the name at the top and the middle to cancel. Favourite is filled when it already is one.
* **Library, hold a song:** Add to queue, Playlist, Favourite, Album, Artist, Info, Tags. Song rows now open on a short tap, so a hold doesn't also play the song. Holding an album or artist still plays it, as before.
* **Folders, hold a song:** Add to queue, Playlist, Favourite, Info, Tags, Rename, Copy / Move, Delete.
* **Folders, hold a folder:** Add to queue (every song under it), Playlist (the whole folder), Tags (fills in the whole folder), Rename, Copy / Move, Delete.
* **Copy / Move:** one step, choosing "Copy here" or "Move here" in the destination picker. The earlier rules still apply: the playing track can't be moved, renamed or deleted, and deleting always asks.
* **Favourites for any song:** a song other than the playing one is favourited directly in the player's favourites table, copying exactly the columns the two tables share. The playing song goes through the player, as before.
* **Info for another song:** Song Info shows that song until it's next opened for the playing track.
* **Tested:** every action from all three menus.

### Folder browser restyle
The Library's look: curved 54 px rows, 18 px names, a format line (FLAC, MP3...) for songs, accent folder icons with a chevron, and the position dots. It keeps its long-folder speed: 3,012 entries at 0.18 ms per scroll step, clean under the sanitizers.

### Smaller changes
* **Song Info bitrate:** a new row, using the library's value when the scanner recorded one, otherwise the file's average (size / length). Tested: 3.6 MB over 3:00 reads 160 kbps.
* **Volume popup:** a tap in the middle of the screen closes it at once. Tested.
* **Equalizer:** two quick taps on a band reset it to 0 dB. A touch only counts as a tap if the finger barely moved, so a swipe followed by a quick tap can't reset anything. Tested with real touch input: a double-tap resets; two slow taps don't; a swipe then a tap doesn't.

### Home: the Ring clock
The clock sits inside the playing track's progress ring, with the cover riding the ring at the current position. Under it: the date and weather (icon + temperature) at 18 px, the title in the album colour, and the artist at 16 px. At the bottom: Library, play/pause in the album colour, and Search. Tapping the title, artist or cover opens Now Playing. The battery arc and status icons stay on the top rim.
* **Efficiency:** the ring and cover move once a second, redrawing only their own small area. A new long title scrolls for 15 s, then settles. The blurred backdrop comes from memory.
* **Idle:** with nothing playing, the title reads "Not Playing" in grey.

### Volume popup: the rim arc
A level arc on the right rim fills from bottom to top in the current accent, with rounded ends and a white handle. Beside it, a badge shows the speaker icon and the value at clock size (46 px), on the player's 0–120 scale. The screen stays visible, lightly dimmed. Drag the arc to set the level; it hides 1.8 s after the last change. Tested: follows each level, including the extremes, and hides itself.

### Now Playing options: an orbit
The swipe-left menu and the "..." menu are now the same orbit, rebuilt for the current track each time it opens. Eight actions orbit the playing cover, which wears its progress ring: Favourite, Playlist, Lyrics, Equalizer (opens the round EQ), Info, Album, Artist and Tags ("This track" or "Whole album"). Full-screen art is no longer in the menu; tapping the cover on Now Playing opens it in every style. An audiobook gets its own four: Chapters, Details, Equalizer, Full art. A favourite shows as a filled red heart, and tapping the cover closes the menu. Tested: the actions, the Tags choice, the audiobook variant, closing, and 59 of 59 taps (31 of 31 for the audiobook layout).

### Screensaver: the Dim Ring clock
A new saver style, **Ring**, made active once (the other five remain in Settings → Display → Saver Style). It's the Home Ring clock in greys: clock, date, weather, and the track title in a darkened album colour, inside a faint progress ring with a dimmed cover on it. With nothing playing, the ring and cover disappear. It redraws once a minute.

### Equalizer: round and parametric-lite
Ten bands as pizza slices, each filled out to its gain: a soft accent fill up to the dashed 0 dB ring, and bright beyond it, with a faint line every 1 dB.
* **Range:** −6 to +6 dB in 0.1 dB steps. Each band's frequency can be moved; the filter type stays peaking with Q 0.7.
* **Editing:** tap a slice to select it, and drag outward or inward for the rough level. The centre shows the band's frequency and gain; tap one to choose what − and + change (0.1 dB, or 1/12 octave, per tap; hold to repeat).
* **Typing values:** tap the active value for a number pad (Hz/kHz, or ±). Long-press the frequency to reset that band.
* **Limits:** each band stays between its neighbours, within 20 Hz–20 kHz.
* **Presets:** the button cycles Off → built-ins → USER1–10. Built-ins are shown in grey and read-only. A USER preset using filters this editor can't represent is shown and never overwritten. Selecting a preset never writes; only an edit does.
* **Old presets:** values beyond ±6 dB keep their stored value until that band is edited.
* **Master gain:** kept exactly.
* **Touch safety:** a touch on a band that isn't selected only selects it. Only a drag that starts on the already-selected band changes its level, and only that band holds on to touches. Everywhere else the normal gestures work, including the back-swipe from the left edge. Tested with real touch input: an edge swipe over a band selects it without changing it, and a drag on the selected band changes it.
* **Tested:** 16 checks against a real preset table, including an exact write and read-back (Hz, 0.1 dB, master), the limits, the number pad, and built-in and advanced presets never being written. Also run under the sanitizers.
* **Speed:** the dial's geometry is worked out once, the first time the screen opens. A change then repaints only that band and redraws only its area, 0.11 ms per change on the host instead of 3.8 ms, with no visible difference.
* **On-device check:** that the player applies fractional gains and moved frequencies exactly. The format accepts them.

### Battery & usage (Settings → Battery)
A sixth Settings button opens a 24-hour dial: midnight at the top, hours clockwise.
* **Outer band:** the battery level, empty at the inner edge and full at the outer. A white line over a soft fill; charging shows green.
* **Amber ring:** when the screen was on. **Red ring:** when music was playing.
* **Centre:** charge now, a time-left estimate from the drain over the last 3 hours of battery-only use, and the screen-on and playing totals. While charging it says "Charging".
* **Previous days:** ‹ › step back through up to 7 days, each shown midnight to midnight with how much battery it used.
* **Recording:** inside the UI, one sample a minute (battery, charging, and whether the screen was on or music played at any moment in that minute). Kept in memory for 7 days and saved to `/usr/data/usage.bin` every 10 minutes and before an auto power-off, about 80 KB at most. Nothing is recorded while the clock isn't set.
* **Drawing:** the dial is drawn once when the screen opens, and again each minute while it's open. The fill is painted per pixel (8.6 ms on the host).
* **Tested:** the unset clock, per-minute flags, saving after 10 minutes, reload, the 7-day roll-off, the time-left estimate, day stepping and the empty state, including a sanitizer run.

### Stability pass
Method: every test harness rebuilt with AddressSanitizer and UBSan and run (14 suites, including a 30,000-song Library walk, a 3,000-entry folder and a 3,000-song playlist), GCC's static analyzer over every UI module at `-O0` (verified live by planting a leak and a file-descriptor leak), and a manual review of threads, file writes and list rebuilds. Found and fixed:
* **Library freeze when returning to a long, scrolled list (serious):** the Library rebuilds on every entry. Clearing the old list clamps its scroll, and that scroll event reached the long-list code with its spacers already freed. The previous code hung in the test (killed at 120 s); fixed, the window is detached before clearing. The folder browser and playlists were written this way from the start.
* **Out-of-bounds read in the tagger's JSON decoder:** a truncated reply ending in `\u12` or a lone `\` stepped past the end of the text (confirmed with AddressSanitizer on the old code). Malformed escapes are now skipped, and a lone surrogate becomes U+FFFD.
* **Memory exhaustion from a corrupt FLAC:** metadata blocks are now capped at 16 MB each and 24 MB in total, before anything is allocated.
* **Auto power-off re-launching `poweroff`:** a failed `poweroff` was re-launched on every loop pass (5 times a second). It now launches once; if the player is still running a minute later, the idle clock simply restarts.
* **Double artwork lookup:** tagging a single track with no cover found looked it up twice. Now once per track (once per album for Fill In Album).
* **Wasted reads in the tagger:** the tagger read the whole file (a CRC of the audio) just to learn whether it had a cover. It now reads only the headers.

### Folder browser: file operations
Long-press a file or folder for a radial menu (the Quick Settings orbit): **Rename**, **Copy**, **Move**, **Delete**. Tap still opens or plays; a hold opens the menu.
* **Delete:** always confirmed; for a folder the dialog says how many files go with it.
* **Rename:** full QWERTY keyboard; names with `/`, `.`, `..` or an existing name are refused.
* **Copy / Move:** pick the destination by browsing folders ("Up one level" is the first row). A name clash asks before replacing. A folder can't go inside itself. Copies are written as `.part` and renamed when complete.
* **Rules:** the playing track, or a folder containing it, can't be changed. Copies and deletes run in the background. Every write goes through the SD write gate. Afterwards the folder refreshes in place and the library re-scans.
* **Tested:** 16 checks on a scratch card tree, including identical copies, the clash prompt, the playing-track guard and no leftover files.

### Auto-tag
Settings → Playback → **Auto-tag** (default Off). When a track has played 10 s, on Wi-Fi (a default route over `wlan`), it's checked from its headers alone. If it lacks synced lyrics or artwork, the tagger adds them in the background, silently unless something was added. Tracks where nothing was found are retried on a later play, at most once a day; the retry log lives in `/usr/data/autotag.log` and stays small. Tested: the 10 s, playing, setting and once-per-play gates, and the retry log.

### Long lists: playlists and the folder browser
The same windowing as the Library, now as a shared `vlist.c`, for lists of 60 rows or more.

| | Before | After |
|---|---|---|
| Folder, 3,012 entries | 3,012 rows, 165 ms to open, 0.74 ms per scroll step | 17 rows, 3 ms, 0.11 ms |
| Playlist, 3,000 songs | 3,000 rows, 144 ms, 0.77 ms per step | 16 rows, 2 ms, 0.12 ms |

Both are pixel-identical to the previous build at five scroll positions, including the very end.

### Lyrics & artwork tagger
Now Playing menu → **Add Lyrics & Artwork** (this track) or **Fill In Album** (every track of its album). Runs in the background with progress toasts.
* **What it adds:** synced lyrics from lrclib.net (matched on title, artist, album and duration, with a search as fallback), replacing plain unsynced lyrics but never synced ones. A 600 × 600 front cover from Apple's iTunes search, only when the file has no picture; one lookup serves a whole album.
* **Where it goes:**
  * **FLAC:** a `LYRICS=` comment and a PICTURE block.
  * **MP3:** ID3v2 USLT (LRC text; UTF-16 in v2.3, UTF-8 in v2.4) and APIC, keeping the tag's version and every other frame. A file without a tag gets ID3v2.3.
  * **Other formats:** a sibling `.lrc`.
* **Safety:** the new file is written to a hidden temp file beside the original, re-read and verified (the audio byte-identical to the original, and the additions read back), fsync'd, then swapped in. Uncommon tag layouts (ID3v2.2, unsynchronised or extended-header tags, damaged FLAC headers) and a nearly full card are skipped, leaving the file untouched. Writes go through the SD write gate.
* **Tested with an independent tagging library (mutagen), 23 checks:** FLAC and MP3 in every starting state (no tag, plain lyrics, existing cover, already complete). Additions read back, other tags and versions kept, audio byte-identical, MP3s still valid with the same length. ID3v2.2 skipped with the file unchanged, OGG left untouched with a `.lrc` written, no temp files left. A second run changes nothing. JSON decoding covers escapes, accents and emoji. The online lookups can only run on the device.

### Search
Rebuilt for the round screen: the query field at the top with Backspace inside it (tap for one character, hold to clear), live results under it, and an alphabetical keyboard of big keys in the wide middle of the circle. Rows are sized so every key sits fully inside the screen: 40 px keys at the top, narrowing to 30 px at the bottom. Results refresh as you type, after a short pause so typing stays smooth; tap one to play it. "123" swaps in digits and common punctuation. Wi-Fi passwords and playlist names keep the full QWERTY keyboard.

### Bluetooth reliability
* **Bounded commands:** every `bluetoothctl` call is time-limited, and a timed-out call is killed with its whole pipeline. A wedged `bluetoothd` used to freeze the UI, or leave the device-list worker stuck so the list never refreshed again.
* **Stale bring-up marker:** a bring-up that died used to leave its marker behind, making every later "turn on" silently do nothing until a reboot. A marker older than 45 s is now cleared.
* **Bounded bring-up:** the background bring-up caps each `bluetoothctl` step at 6 s, so it always finishes.
* **Non-blocking off:** turning off no longer waits on `bluetoothctl`; stopping the stack and blocking the radio switch it off.
* **One truthful state:** off, turning on or on, shared by the Home icon, the Quick Settings tile and the toggle. "On" means the radio is unblocked and `bluetoothd` is running. The Quick Settings tile shows a red ring while Bluetooth comes up and follows it every second.
* **Tested:** the bounded runner (normal output, a hanging command, a hanging pipeline killed as a whole), stale-marker cleanup and the process check. The radio itself can only be tested on the device.

### Auto power-off
Settings → System → **Auto Power-Off**: Off, 10, 20, 30 or 60 minutes (default Off).
* **When the clock runs:** only while nothing plays, nobody touches the player, it's in Local mode, the card isn't handed to a PC, no library scan runs and the keyboard isn't open. Anything else restarts it. If music is still playing when you fall asleep, the Sleep Timer stops it and the idle clock starts from there.
* **Warning:** with the screen lit or dimmed, the last 30 seconds show a countdown that any tap cancels. With the screen fully off nobody is looking, so it powers off at the limit.
* **The shutdown:** `sync; poweroff`, verified on the device to switch it off cleanly.
* **Tested:** the timing rules (nothing before the countdown, a countdown from 30 s, off at the limit, no countdown with the screen off, Off never acts) and tap-to-cancel.

### Efficiency
Nothing here changes how the UI looks, apart from long titles settling.
* **Wake-ups:** the main loop polls every 5 ms only while a finger is down. Previously any running animation (a spinner, a pulse, a scrolling title) kept it waking about 200 times a second, although the screen redraws at most about 30 times a second.
* **Screen off:** with the panel fully off, nothing is drawn; drawing resumes with one full redraw on wake. Tested: half a second of spinning and scrolling produced no frames with drawing paused, and waking redrew the screen.
* **Low battery:** the pulse on Home and in Quick Settings breathes three times, then stays steadily red, instead of pulsing for as long as the battery is low.
* **Long titles:** a title that doesn't fit scrolls for 15 s after a track change or when Now Playing appears, then settles with "...". Tested: scrolls, settles, and scrolls again on the next track.
* **Cover decoding:** the full-size cover is decoded only for the Poster style, or when immersive mode opens (Ring), instead of on every track change in every style.
* **Blurred backdrops:** decoded once per track into memory, so repaints on Home, Now Playing and Quick Settings no longer re-read the image file. Tested pixel-exact against the file.
* **Long Library lists:** lists of 80 rows or more keep only the rows around the screen, about two dozen, between two spacers that give the list its full height. They're the same rows, built by the same code, so scrolling, momentum, the A–Z jump, the position dots and the song focus work as before. Measured on the host with 30,000 songs, compared with the previous build:

  | | Before | After |
  |---|---|---|
  | Ready to show | 6.8 s | 2 ms |
  | Extra memory | 49 MB | 1.2 MB |
  | One scroll step | 15.9 ms | 0.17 ms |

  Checked: screenshots at five scroll positions, up to the very end of the list, are pixel-identical to the previous build. A walk down and back up the whole list verified every row on screen (545,559 checks) for position and song.
* **Short lists:** no longer re-lay out the whole list and re-curve it on every fill step; they do it once, when the fill finishes.
* **Draw buffers:** 120 rows instead of 60 (2 × 173 KB): full-screen redraws, such as screen changes and transitions, measured about 10% faster.
* **Immersive:** the record stands still while playback is paused, and its timer slows down. It reads the play state the main loop works out; the player's raw status field reports 0 even while playing.

### Now Playing: Ring style
A fourth Now Playing style, **Ring** (Settings → Display → Now Playing), next to Cover, Vinyl and Poster:
* **Cover and seek:** a round cover with the progress ring hugging it, in the album colour; drag around the ring to seek. The seek recogniser now takes its centre, radius and grab band from the active style, so the ring's band stays clear of the title, the icons and the middle of the cover.
* **Icons:** the heart at the ring's left apex and the play-mode toggle at its right apex, level with the ring's centre.
* **Title and artist:** larger (24 px and 16 px), scrolling when too long. Tapping them opens the album with the current song highlighted, placed inside its artist: the header's back arrow walks up (album → the artist's albums → Artists → Library), while the back swipe returns straight to Now Playing.
* **Artwork:** tapping it opens immersive mode.
* **Transport:** a larger play/pause (36 px) in the album colour, lower down, with the times beside Previous and Next.
* **Tested:** seek mapping round the ring (12, 3, 6 and 9 o'clock and a point between read 0, 250, 500, 750 and 125 of 1000); taps on the title, heart and cover centre don't start a seek; the artwork tap opens immersive mode; switching back to another style restores its geometry.

### Orbit layout (Quick Settings, Settings, Working Mode)
A shared `orbit.c` replaces the radial wheels: round buttons with captions circle a central hub whose ring carries the state. Taps go to ordinary buttons with an 8 px padded target, so each lands on the button pressed (tested: 45 of 45 and 38 of 38 taps, including the padding and the hub).
* **Quick Settings:** six toggles (Wi-Fi, Bluetooth, Library, Rescan, Mode, Settings) around the playing cover, inside a progress ring in the album colour. A tap on the cover goes Home; the centre button plays or pauses. Brightness stays on the top rim (control arc, rounded ends). The bottom rim is now a battery arc with the Home screen's logic: white, red with a slow pulse at 15% and below. The album's blurred backdrop sits behind, dimmed. Holding Wi-Fi or Bluetooth still opens its settings.
  * **Battery icon:** a battery glyph under the bottom arc, mirroring the sun under the brightness arc. It shows the level in steps, a bolt while charging, and turns red at 15% and below.
  * **Rescan while scanning:** the Rescan icon turns red and spins, and its caption reads "Scanning", for as long as a library scan runs, including one started from Settings.
* **Settings:** the five categories around a grey-ringed hub; tapping the hub goes back.
* **Working Mode:** the six modes around a hub showing the active mode inside a solid red ring; the active mode's button is filled red. A tapped mode gets a red ring while the hub's ring spins, until the player confirms.

### Design system (`theme.h`)
Every shared value now lives in one header, and the touched screens use its names instead of raw numbers:
* **Colour:** background, two surface levels, the unfilled-arc track, three text levels and the accent. The rule: red means "on" or selected; anything showing the playing track uses the album's own colour (`ui_media_accent()`), which now includes the home pill, its thumbnail and progress outline, and the Quick Settings mini-player. Levels (brightness, volume) are white.
* **Type scale:** clock 46, poster title 28, screen title 20, list 18, detail 16, caption 12. The date, Settings rows, tile captions and Now Playing's times moved onto it. The oversized play/pause on Now Playing is a deliberate exception.
* **Shapes and rims:** row radius 14, card 18, pills fully round. Indicator arcs 3 px, control arcs 10 px (Quick Settings keeps its rounded ends). Top rim for status and level, bottom rim for time and amount, left rim for position in a list, right rim for jumping.
* **Icons:** one family, Font Awesome 4.7. A small generated font (`font_theme_20.c`) adds the magnifier, the record and the solid heart, replacing the hand-built search and record icons. The heart is always solid; the colour carries the state.
* **Lists:** a reusable `curvelist.c` gives any list the Library's curved rows and left-rim position dots. Settings lists use it now, with rows on the list and detail sizes.
* **Quick Settings:** tile captions brighten with the tile when it's on. Tapping the mini-player now goes Home instead of Now Playing.


### Home
* **Battery arc:** a thin arc across the top rim shows the charge. At 15% or below it turns red and pulses slowly.
* **Status row:** Wi-Fi and Bluetooth glyphs (shown only when on) and a battery glyph sit in one centred row. The percentage is gone, since the arc shows the level.
* **Clock:** large clock, with the date and weather stepping down beneath it.
* **Now Playing pill:** centred and wider (304 px). A red progress line traces its outline: it starts at the cover circle, runs along the top and back along the bottom, and closes when the track ends.
* **Library and Search:** paired as one row of two pills.
* **Backdrop:** the blurred album-art background is unchanged.

### Quick Settings
* **Brightness arc** on the top rim.
* **Volume arc** on the bottom rim, using the player's 0–120 scale. It follows the hardware buttons live.
* **Mini-player** with cover, title, artist and play/pause. Tapping the pill opens Now Playing.
* **Six tiles:** Wi-Fi, Bluetooth, Library, Rescan, Mode and Settings. The Mode tile's icon shows the active working mode.
* **Upstream's palette is replaced:** this is a fixed layout. The upstream Quick Settings config screen still opens and explains that.

### Gestures
* **Swipe up from the bottom edge goes to Home.** It must start below y=320 and travel twice the normal swipe distance. It's disabled on Home, the screensaver and Now Playing.

### Radial menus (Working Mode and Settings)
* **Component:** a new `radial.c` draws a "pizza" menu: slices around a central hub, sized for the round panel. The hub is Back, alongside the usual swipe.
* **Tap detection:** one transparent layer over the wheel works out which wedge was tapped from the touch point's angle and distance from the centre. LVGL's own arc hit test pads every arc by about 50 px worth of angle, so neighbouring slices overlapped and the topmost one stole taps: in a test tapping every slice at its centre, edges and both radii, the first version landed 17 of 44 taps correctly, and this one lands 44 of 44 (37 of 37 for the five-slice Settings wheel).
* **Working Mode:** six slices, clockwise from the top: Local playback, USB DAC, BT DAC, BT streaming, AirPlay, USB storage. The active mode's slice is outlined in the accent, with its icon in the accent, and the hub shows its icon. While a switch settles, the slice gets a faint outline and the hub shows a spinner.
* **Settings:** the five categories as slices (Playback, Audio, Display, Network, System), with icons in the accent.

### Working Mode
* **Switching logic:** upstream's code is kept unchanged, including the "switching" state, the settle timer and the readback of the gadget state. Only the presentation changed.

### Settings
* **Categories:** shown as the radial menu above. Rows inside each category keep the thin red accent bar.

### Library
* **Artist > Album > Track:**
  * **Artist screen:** tapping an artist shows Play All / Shuffle, an "All Songs" row, and that artist's albums with track counts.
  * **Hold to play:** holding an album plays it; holding All Songs plays the whole artist.
  * **From Now Playing:** "Go to artist" lands on the artist's album list.
* **Track order:** albums list tracks by disc, then track number, shown as "3.", "5.", "8.". Albums without track tags stay A–Z. Upstream's scanner already stores these numbers; the library now reads and uses them.
* **Position dots:** eleven dots on the left rim, opposite the A–Z button, show where you are in the list. The current one is red and moves as you scroll. They only appear when the list is longer than one screen.
* **Larger text:** list titles are 18 px (up from 16) and details 16 px (up from 14), in rows 6 px taller. Subtitles now stop short of the duration column instead of running under it.
* **Curved lists:** rows narrow with the circle, snapped to five width bands. Only visible rows are updated, and only when they cross a band, so text is rarely re-measured. On the host this measured roughly 10% over a flat list.

### Lyrics
Lookup order is now:
1. **Lyrics embedded in the file:**
   * **MP3:** ID3v2 `USLT` or `SYLT`, including v2.2 `ULT`/`SLT`, in any text encoding including UTF-16.
   * **FLAC:** the Vorbis comment `LYRICS` or `UNSYNCEDLYRICS`.
   * **Speed:** large cover-art frames are skipped rather than read.
2. The sidecar `.lrc` file.
3. Online lookup.

Timestamps in embedded lyrics are stripped the same way as in `.lrc` files.

### Now Playing: Poster style and immersive mode
A third Now Playing style, **Poster**, is added next to Cover and Vinyl (Settings → Display → Now Playing) and becomes the default once. Cover and Vinyl are unchanged and restore exactly.
* **Background:** the sharp full-screen cover, lightly dimmed, with a soft band behind the title and a heavy fade over the bottom third.
* **Text:** off-white with drop shadows, so it reads on pale covers. Long titles wrap to two lines, then end in "…".
* **Colour:** controls and arcs use the album's own accent even when the global accent is fixed. Near-black or grey picks fall back to the global accent.
* **Right side:** favourite heart above the play-mode toggle, which cycles through sequential, shuffle and repeat as before.
* **Left side:** a record button that opens immersive mode.
* **Title room:** the side icons sit at the rim, so the title gets 236 px, and the page dots are hidden so nothing sits on the volume arc.
* **Transport:** glyphs only, no button backgrounds. Play/pause takes the album accent.
* **Seek arc:** on the bottom rim, filling left to right, with the same drag-to-seek. The recogniser takes its geometry and direction from the active style, and a test confirms the left end reads 0, the bottom centre 500 and the right end 1000 out of 1000. There's no volume arc on this screen; the hardware buttons still show the volume bar.
* **Times:** elapsed sits beside Previous and remaining beside Next, level with the two ends of the arc and clear of its stroke, with no slash.
* **Tap the title** (anywhere on the title and artist block) to open the song's album in the Library, scrolled to the song and highlighted with a lifted row and accent title. The highlight survives the Library's own rebuild when it appears, and is dropped on your next move there (a tap, a drill-in or Back); reopening the album normally shows it plain. Tracks without an album tag open the artist instead.
* **Performance:** the cover is decoded once per track into RAM, since as a file source it was re-decoded behind every repaint. Everything on the Poster that never moves (the dim, the top whisper, the band behind the title and the bottom fade) is also drawn into the picture once per track, in the display's native pixel format. Five blend layers under every repaint become one straight copy. The once-a-second progress update on the host went from 4.75 ms (file source) to 0.19 ms (RAM) to 0.09 ms (baked), the same as the plain Cover style.

**Immersive mode** (the record button; tap anywhere to leave):
* **Cover:** the sharp 364 px cover (the one the Poster already decodes), converted once to the display's native pixel format and spun 1:1 like a record at one turn per 20 s.
* **Grooves and centre label:** drawn into the cover image itself once per track. They're perfectly round, so they look the same spinning. As live objects they took about 30% of every frame; baked in, a full frame on the host went from 3.49 to 2.93 ms at the same quality.
* **Frame rate:** it asks for 24 fps. The angle follows the clock, so the record turns at the right speed whatever rate the device holds. Smoothing is on while the device keeps about 18 fps or better, and switches off once if it can't, which halves the cost of a frame.
* **Header and progress:** title · artist in a small line at the top, and a thin progress arc on the rim.
* **Synced lyrics:** shown in the bottom third inside a dark fade, at 20 px for the current line and 16 px for the lines either side. Long lines wrap instead of being cut (up to four lines). The block is stacked from the bottom, so a long line pushes the previous one up, and the fade grows with it so every line stays readable.
* **Lyrics sources:** embedded timed lyrics or the sidecar `.lrc`, with repeated timestamps and metadata tags handled. Tracks without timed lyrics show a short note.
* **Cost, per frame on the host:** the old 148 px upscaled spin was 1.56 ms, the sharp cover without smoothing is 1.51 ms, and with smoothing 3.08 ms.

### Theme
* **Accent colour:** one red, `#E4122C`, replaces the mixed blue and pink accents. That covers switches, sliders, "on" states, Wi-Fi and Bluetooth checkmarks, the EQ, the A–Z index and progress rings.
* **First start:** the accent is set to fixed red once. A later choice under Display → Accent Colour, including Dynamic, sticks.

---

## Testing
* **SD fix:** verified on hardware with a card left inserted across reboots.
* **Controller re-announce:** tested against a fake sysfs tree with the SD slot and SDIO Wi-Fi on separate controllers. It picks the SD controller and never the Wi-Fi one, and does nothing when no card is present.
* **Library:** 20 host-side navigation checks pass, against a sample database, with curved rows and track ordering.
* **Embedded lyrics:** host tests cover ID3v2.3 Latin-1 with a 600 KB cover, ID3v2.4 UTF-16 with LRC timestamps, SYLT, FLAC `LYRICS` after a 400 KB picture block, FLAC `UNSYNCEDLYRICS`, and files with no lyrics.
* **Timed lyrics:** host tests cover a sidecar `.lrc` with metadata tags, a repeated-chorus line and mixed `[mm:ss]`/`[mm:ss.xx]` stamps, embedded UTF-16 LRC, and FLAC LRC. Untimed or missing lyrics return nothing.
* **Now Playing:** rendered from the real `ui.c` in Poster, long-title and immersive modes, and Cover style checked to restore unchanged.
* **Screenshots:** every screenshot is rendered from this code with the real LVGL build.
* **Build:** clean, apart from two unused-function warnings in `home.c` that upstream already has.

## Known limitations
* **Quick Settings:** upstream's user-configurable tiles are removed in favour of the fixed panel.
* **Curved lists:** applied to Library lists only.
* **New work modes:** AirPlay and BT streaming were verified on one device, V2.40.
* **Poster and immersive:** not yet run on hardware; the spin rate and the fade strength may want tuning on the real panel.
* **Held back from this release:** two changes from development, a clean unmount before a USB Storage export and a boot-time SD trace. They're parked while the cold-boot fix was isolated.

diskOS is by b0hemia and contributors, GPL-3.0-or-later. These changes are offered under the same licence.
