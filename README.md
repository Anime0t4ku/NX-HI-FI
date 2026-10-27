# NX Hi-Fi

NX Hi-Fi is heavily based on my MiSTer Hi-Fi project, adapted for Nintendo Switch homebrew. It brings SD-card and network music playback, album artwork, an equalizer and remote control for other NX Hi-Fi devices and MiSTer Hi-Fi.

Supported audio formats and codecs: **MP3, FLAC, PCM WAV and Ogg Vorbis**.

## Getting started

1. Copy `nx-hifi.nro` to `sdmc:/switch/nx-hifi/` on your Switch SD card. If your download includes an `sd-card` folder, copy its contents to the root of the SD card instead.
2. Put your music in `sdmc:/music/`, or choose another music folder in Settings.
3. Open NX Hi-Fi from the Homebrew Menu. Full-memory application mode, also called title override, is recommended.
4. Choose a source and select a song.

For network features, connect your Switch to the network through its system settings first. Existing settings are kept when you replace the NRO.

## Playing music

Play supported music files from the SD card or an SMB network share.

Selecting a song plays the supported music files in that folder, starting from your selection. Use the player controls to pause, skip, stop, change volume, shuffle or repeat. Repeat cycles between Off, Repeat All and Repeat One.

You can keep browsing while music plays. Open the fullscreen player by pressing **R3**, tapping the artwork or selecting the expand button. Press **B** or **R3** to return. Tap or drag the progress bar to seek when a duration is available.

The layout adjusts automatically between handheld and docked use. NX Hi-Fi must remain running to play music; it does not provide background music while playing Switch games.

## Controls

| Control | Action |
| --- | --- |
| L / R | Switch sections |
| D-pad / left stick | Move through lists |
| A / B | Select / Back |
| Left / Right | Jump through lists, adjust EQ, or select a player control |
| Plus | Play / Pause |
| ZL / ZR | Previous / Next song |
| Y | Toggle shuffle |
| Minus | Switch between browsing and player controls |
| R3 | Open or close fullscreen |
| X | Edit the selected source or remote client; otherwise stop playback |
| Touch | Select items and use player controls |

While player controls are focused, Up/Down seeks in five-second steps. On-screen hints show the controls available on each screen.

## Music from a network share

Open **Sources > Add SMB share**, enter the server and share details, test the connection and select **Save share**.

For a network folder such as `\\NAS\Music\Albums`, enter:

- **Server:** `NAS` or its IP address
- **Share:** `Music`
- **Subfolder:** `Albums` — optional

Enter a username and password, or enable Guest if your server allows it. SMB2/SMB3 shares are supported. Select a saved share and press **X** to edit or remove it.

## Artwork and appearance

Artwork is read from supported MP3 and FLAC files. You can also place `cover.jpg`, `folder.png` or `front.jpg` beside your music; JPG, JPEG and PNG variants are supported.

Settings includes Light, Dark and OLED themes, accent colours, artwork preferences, a visualizer, a clock and a screensaver. The Equalizer section provides presets and manual adjustment of five frequency bands.

Settings are saved automatically. The screensaver turns the display black while playback continues; press a button or touch the screen to wake it.

## Control NX Hi-Fi from a browser

Enable **Web Remote** in Settings and open the displayed address on a phone or computer on the same network:

```text
http://SWITCH-IP:8182
```

The Web Remote controls playback on the Switch. It also provides music browsing, artwork, equalizer controls, settings and network-share management. NX Hi-Fi must stay open for the Web Remote to work.

## Control another device

Open **Remote** and add an NX Hi-Fi or MiSTer Hi-Fi client using its IP address. You can also choose **Scan for remote clients**. The target must be running with its Web Remote enabled on port **8182**.

### Another NX Hi-Fi device

Choose between:

- **Independent control:** browse and control the target through your own interface.
- **Screen control:** follow and control the target's current interface, when supported by that target.

In Screen control, hold **Minus for three seconds** to open the local menu. Use it to disconnect or switch modes. Disconnecting leaves music playing on the target.

### MiSTer Hi-Fi

MiSTer uses Independent control. You can browse its available sources, control playback and edit its settings, including Plex, Jellyfin, Emby, SMB and online radio setup.

Remote Settings is organised into Playback, Appearance, Controls, System and Experimental. Select a category with **A** and return with **B**.

GBA Music is available when enabled on the MiSTer. It is experimental, and songs take time to prepare before playback. Preparation messages and errors appear in the remote interface.

The remote player uses a decorative animated visualizer; it does not react to the MiSTer's audio. Virtual CD and Screen control are not currently available for MiSTer remote use.

Playback stays on the device you control. Remote features depend on the target's capabilities; they do not add those audio formats or sources to local Switch playback.

## Helpful notes

- **Local format support:** M4A/AAC/ALAC and Opus are not supported for local playback on the Switch. Some files may have limited seeking or an estimated duration.
- **Connection problems:** check that both devices are on the same network, the address is correct and the target's Web Remote is enabled. You can retry an offline client or return to the local player.
- **Settings and passwords:** configuration is stored in `sdmc:/switch/nx-hifi/config.json`. It can contain saved network credentials, so do not share your personal configuration file. Leave a saved password field blank to retain it where indicated.

## License

NX Hi-Fi is developed by **Anime0t4ku** and released under [GPLv3](LICENSE). Third-party components retain their own licenses and notices.

Music and artwork are not included.
