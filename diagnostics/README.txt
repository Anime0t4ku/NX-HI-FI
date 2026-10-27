NX Hi-Fi v0.9.0 - remote modes and discovery

Replace the NRO where you normally launch NX Hi-Fi, then fully close/reopen it.
Enable the web remote on each target. The fixed address is http://SWITCH-IP:8182.
Old server ports and saved client ports migrate to 8182. Settings and credentials
are retained. Refresh browser pages to load the updated web interface.

Remote tab: choose a saved client, add one manually, or scan. An offline
client shows Retry/Back. Online NX clients offer Independent control or Screen
control; the target needs v0.9.0 for Screen control. MiSTer uses its existing
Independent control API. Local and remote playback remain independent.

Screen control always has a local Disconnect button. Hold Minus for 3 seconds
for Disconnect, Switch mode and Cancel. Short Minus presses reach the target
on release. Disconnect leaves target music playing. Text prompts appear on
the controlling Switch/browser; password text is not exported in drawing state.

Boot marker: boot.remote-modes-0.9.0.
Display changes: display.docked (1920) or display.handheld (1280).
If needed, send switch/nx-hifi/playback-debug.log. It omits filenames, server
addresses, credentials and track metadata. Debug symbols are for analysis only.
Host and browser tests passed; physical Switch/MiSTer confirmation remains.
