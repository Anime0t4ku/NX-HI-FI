# NX Hi-Fi — v0.9.0

Nintendo Switch homebrew music player. A new C++ implementation with a GPU-accelerated SDL2 interface, using MiSTer Hi-Fi's behavior as a reference.

## Install and run

1. Copy the **contents of `sd-card/`** to the root of your Switch SD card. The application belongs at `sdmc:/switch/nx-hifi/nx-hifi.nro`.
2. Put music in `sdmc:/music/`, or change the music folder in the app's Settings screen.
3. Launch NX Hi-Fi from the Homebrew Menu. Full-memory application mode / title override is recommended for graphics, artwork and audio buffers.
4. Open **Sources > Add SMB share** to configure a network share. Enter your credentials (password entry is masked), or explicitly enable Guest access, select **Test connection**, then scroll to **Save share**.

No MiSTer, external server application, or manually edited JSON file is required. An existing SMB2/SMB3 server is required for network music. Join your network through the Switch system settings first.

## Features

- SD-card and SMB2/SMB3 folder browsing.
- Streaming network reads with a 512 KiB read-ahead cache and background decoding. Playback does not first download the entire track.
- MP3, FLAC, PCM WAV and Ogg Vorbis playback, converted to stereo 44.1 kHz float PCM before the platform audio output. Ogg Opus is not supported.
- Play a folder's tracks starting at the selected item, previous/next, pause/resume, Stop, shuffle, Repeat All and Repeat One.
- Seeking with ten-second steps, plus touch seeking when duration is available. MP3 duration uses Xing/Info/VBRI frame counts when present. Otherwise a bounded CBR check supplies an estimated duration marked with `~`; files without reliable duration information retain ten-second seeking. There is no full MP3-file scan. Some files/decoders may not support accurate seeking.
- Embedded MP3 (ID3v2.2/2.3/2.4) and FLAC picture artwork, then folder fallback matching MiSTer Hi-Fi: `cover`, `folder`, `front`, each with `.jpg`, `.jpeg` and `.png`, case-insensitive. Invalid candidates fall through to the next. Artwork loads independently of decoding, with limits of 16 MiB compressed, 4096 pixels per side and 16 megapixels.
- Five-band EQ (60 Hz, 250 Hz, 1 kHz, 4 kHz and 12 kHz), ±6 dB adjustment, manual controls and 11 presets. Conservative automatic headroom prevents boosted presets from clipping at the expense of output level.
- Appearance: Light (white), Dark (dark grey, default), and OLED (black). Accent colour defaults to red, with ten presets and a Custom RGB/hex colour picker. The picker previews changes live; Save commits them and Cancel restores the previous colour. Themes and accent colours are saved on the SD card. Text contrast adapts to the chosen theme.
- Red headset/play-button Homebrew Menu icon.
- Optional fullscreen player: tap album artwork, tap the expand icon in the player controls, or press R3; B, R3 or tapping the large artwork returns to the same browsing/editor screen. Tabs and browser navigation are hidden while the player is fullscreen. All touch and controller playback controls remain available, including seeking, volume, shuffle and repeat. Fullscreen includes a live spectrum, technical metadata in individual blocks, and title scrolling. Tap or drag the progress bar to seek. Minus switches between browsing and playback controls; while controls are focused, Up/Down seeks forward/back five seconds and Left/Right chooses an icon.
- Track title, artist and album from MP3 ID3v2.2/2.3/2.4, FLAC/Vorbis comments and WAV RIFF INFO tags. UTF-8, Latin-1 and UTF-16 tags are supported; missing titles/albums fall back to the filename/folder. Fullscreen also shows source format, native sample rate in kHz, bitrate and FLAC/WAV bit depth where available. These describe the source file, not the 44.1 kHz audio output.
- A live audio spectrum, now-playing panel, touch support and Joy-Con/Pro Controller navigation. Automatic 1280×720 handheld and 1920×1080 docked output. The TV layout shows 11 list rows instead of 8, wider text columns, larger square artwork and crisp, TV-sized text. Docking/undocking reflows the local and Remote screens while retaining playback, page, selection and fullscreen mode; no restart or display setting is needed.
- Saved SMB configurations, guest access, optional domain and subfolder, text-field editing, connection testing and share removal.
- Display settings include a clock using the Switch’s system time and timezone (12/24-hour), artwork visibility, automatic hiding of missing-artwork placeholders, folder-artwork priority, visualizer visibility and a black screensaver. The first input wakes the screensaver without activating the control underneath. Playback continues.
- List wrapping and hold-to-repeat are always enabled. Playback can optionally remember shuffle/repeat between sessions.
- Built-in web remote with desktop/mobile layouts matching the app, synchronized theme/accent/settings, SD/SMB source management and testing, playback, artwork, metadata, spectrum, seeking, volume and all EQ controls/presets.
- Saved playback/settings state in `sdmc:/switch/nx-hifi/config.json`, with a backup for interrupted writes.

The five sections are Sources, Library, Equalizer, Settings and Remote. Playback retains its own internal track list while you browse other folders; it has no separate Queue tab.

The app opens on Sources until you choose a source. Playback buttons use MiSTer Hi-Fi’s geometric icons, including shuffle and repeat off/queue/one. All controller hints appear in the bottom bar. The version sits directly beside the NX Hi-Fi title. Use HOME to leave the app.

## Controls

| Control | Action |
| --- | --- |
| L / R | Change section; leave fullscreen when changing section |
| D-pad up/down or left stick | Move selection with wrapping and hold-to-repeat; while controls/fullscreen are focused, seek forward/back five seconds |
| A | Open folder, play track, edit a field, confirm a setting or activate the focused playback icon |
| B | Leave fullscreen; otherwise parent folder / Sources, or cancel share editing |
| Left / right | Jump five list entries; in controls/fullscreen select playback icons; adjust an EQ band |
| Minus | Focus playback controls / return to list |
| ZL / ZR | Previous / next track |
| X in Sources | Edit the SD music folder or selected SMB share; on Add SMB share, open the editor |
| X in Remote | Edit the selected saved client |
| X elsewhere | Stop playback |
| Y | Toggle shuffle |
| Plus | Play / pause |
| R3 (press right stick) | Toggle fullscreen player |
| Touch | Select tabs, open folders/tracks, edit fields and use player controls; drag the fullscreen progress bar or RGB sliders |
| Vertical swipe on the list | Scroll the selection |

Cancelling text entry leaves the existing value unchanged. Share fields that are unnecessary for guest access are shown as “Not required.” Edit actions are committed only by selecting **Save share**. Remove share is an explicit action at the end of an existing share's editor.

## SMB details

Enter the host/IP separately from the share name. For example, a Windows path `\\NAS\Music\Albums` becomes:

- Server: `NAS` (or its IP address)
- Share: `Music`
- Subfolder: `Albums` (optional)

Browsing and playback use separate SMB contexts, so browsing does not seek the playing file or share a non-thread-safe connection with the decoder. Remote reads are performed on the decoding thread, not on the real-time audio/UI path. Requests use libsmb2's five-second SMB timeout; name resolution and service shutdown may take longer. An in-flight network call can delay exit, but source navigation remains asynchronous.

Credentials are stored as ordinary JSON on the SD card. Password masking only hides them in the interface. Do not share your actual config file. The supplied `config.example.json` has no servers or credentials and does not overwrite existing settings.

## Web remote

In **Settings > Remote**, enable **web remote**. Open the displayed address on a phone or computer on the same network, normally `http://SWITCH-IP:8182`. The port is fixed at 8182; existing configurations and saved profiles migrate automatically. The service runs inside NX Hi-Fi while the app is running; it is not an always-on Switch sysmodule.

The browser has Sources, Library, Equalizer, Settings and Remote, plus the regular/fullscreen player. Every setting is shared with the Switch and other connected browsers, including themes, custom colours, clock format, artwork preferences, screensaver and remote settings. Changes apply live. Playback controls stay in the player rather than Settings.

Use Sources to edit the SD music folder or add/edit/test/remove SMB shares. Saved passwords are never returned by the API. Leaving a password blank keeps the saved password; use **Clear saved password** to erase it. Concurrent source edits are checked before saving, and changing sources does not stop the playing track. Browsing must select a source again after a source configuration changes.

The remote controls audio playback on the Switch. Its artwork is resized to at most 512 pixels for transfer, and it uses the Switch's clock. Browser shortcuts are Space (play/pause), Left/Right (seek), Shift+Left/Right (previous/next), F (fullscreen), and Esc (browse). The browser follows the same configured screensaver timeout, with its own display inactivity timer.

## Build from source

Install the official devkitPro Switch environment first. With devkitPro pacman available on Linux/WSL:

```sh
sudo dkp-pacman -Syu
sudo dkp-pacman -S --needed switch-dev switch-sdl2 switch-sdl2_ttf switch-sdl2_image switch-cmake
```

A native `cmake`, `make` and C/C++ compiler are also needed. On Debian/Ubuntu/WSL, install them using your system package manager:

```sh
sudo apt-get install build-essential cmake
```

Then:

```sh
chmod +x build-switch.sh scripts/test-host.sh
./scripts/test-host.sh
./build-switch.sh
```

On devkitPro MSYS2, use `pacman` without `sudo` for the devkitPro packages. Run the build script from the devkitPro shell. Files use LF line endings.

The build compiles the included libsmb2 source into a **project-local** dependency prefix; it does not require administrator access to install libsmb2 into the SDK. It then produces `nx-hifi.nro` and assembles `sd-card/switch/nx-hifi/`. No Markdown guide or external artwork file is a build dependency.

The GitHub Actions workflow builds the `.nro`, runs the host regression tests, and uploads an SD-card ZIP. Its devkitPro container/packages are resolved by the official package manager, so a future SDK update can require compatibility adjustments. See BUILD_INFO.txt for the toolchain used for the included binary.

## Validation and prototype limits

The included binary has been cross-compiled and linked against devkitPro/libnx. Host tests exercise Unicode ID3 tags, FLAC/Vorbis/WAV metadata, source audio details, embedded ID3/FLAC artwork extraction and malformed metadata, file-path validation, cached read/seek behavior, configuration/credential persistence, backup recovery, EQ parameters, actual WAV-to-PCM decoding/seeking, setting validation/persistence, password redaction/retention and HTTP route/dispatch/shutdown behavior. These checks are not a substitute for Switch testing: the v0.9.0 controller layout, audio output, text entry, Switch clock/timezone, on-device remote networking and NAS interoperability need hardware regression testing.

This version does not include Plex, internet radio, AAC/ALAC/M4A, gapless playback, playlist-file import, library indexing, queue reordering/removal or physical/virtual CDs. Files without supported tags use filenames and folders for the title and album. Playback is inside NX Hi-Fi; it is not a background-music sysmodule for Switch games. No Nintendo system modifications or copyrighted assets are included.

## Dependencies and licensing

The project uses GPL-3.0, matching the MiSTer Hi-Fi reference project. Third-party notices remain in their source files and the `licenses/` directory.

- libnx and devkitPro's SDL2, SDL2_ttf, SDL2_image and codec/font dependencies are provided by the SDK.
- libsmb2 6.2 source is included under LGPL-2.1; the application can be rebuilt/relinked with a modified library using the supplied build script. Its original notices and license are in `third_party/libsmb2/`.
- miniaudio 0.11.25 provides decoding/resampling only; SDL2 supplies platform audio output. Its license alternatives are retained.
- stb_vorbis provides Ogg Vorbis decoding; its original license is retained.
- nlohmann/json 3.11.3 provides configuration handling under MIT.
- Fonts come from the Switch's shared system font service at runtime. No Nintendo fonts are distributed.

## Saved remote clients

Remote saves editable NX Hi-Fi and MiSTer Hi-Fi profiles on fixed port **8182**. Enable the target's web remote and enter its host/IP. X edits a saved client. **Scan for remote clients** searches the local IPv4 subnet (bounded to the local /24 on wider networks), verifies each device's Hi-Fi API, shows progress and Saved markers, and lets you connect or save/edit a result. Manual entry remains available. Cancel stops a scan.

Connect verifies the target first. An unreachable client shows **Remote client is offline**, with Retry and Back. An online NX target opens a chooser:

- **Independent control:** the existing native browser/settings/player view; target screen navigation stays independent. Playback remains on the target. Remote > Disconnect returns locally. Connection loss reconnects without redirecting commands to local playback.
- **Screen control:** requires NX Hi-Fi v0.9.0 on the target. Recreates its current interface from live drawing state, forwards navigation/touch/seek input, and follows tabs, selection, choice dialogs, themes and fullscreen changes. Text entry opens on the controlling Switch (or browser); password values are masked and never exported in drawing state. Local and remote screen changes follow the same target interface. The local header always has Disconnect. Hold **Minus for 3 seconds** for Disconnect, Switch mode and Cancel; a short Minus press goes to the target only on release. Disconnect leaves target playback running. Controls and the local menu stay available when the target goes offline.

MiSTer Hi-Fi connects using Independent control and its existing API. It retains its current source/settings/playback support; Screen control requires a future MiSTer implementation. NX state and screen changes publish at a 50 ms interval plus network/render latency. MiSTer uses its existing 500 ms updates and two-second settings refresh. Its current API does not transmit spectrum, sample rate/bit depth or clock; unavailable data is not fabricated.

The web Remote tab also supports discovery, availability checks and the mode chooser. A target's `/screen` page exposes Screen control with mouse/touch, text prompts, a local menu and keyboard input: Enter = A, Escape = B, X/Y, Space = Play/Pause, Q/E = tabs, [/] = previous/next, arrows = D-pad, F = fullscreen. Hold the minus key for 3 seconds for its local menu. Browser text uses the browser's fonts.

Both native player sizes show source-format/sample-rate/bit-depth/bitrate badges where the source supplies them. Compact artwork and visualizer spacing leave room for metadata blocks in handheld as well as docked layouts. Artwork, spectrum, settings and native networking still require physical Switch regression testing.
