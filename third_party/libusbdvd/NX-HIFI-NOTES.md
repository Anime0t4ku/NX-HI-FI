Vendored from https://github.com/proconsule/libusbdvd
Upstream commit: c7598a9eed32cd3ddf8d3c0120d6ca2312c8008d (v0.1.3 headers).
See LICENSE for upstream licensing.

NX Hi-Fi uses the USB BOT/SCSI classes directly, without mounting libusbdvd's
pseudo filesystem. CDDA sectors are exposed through NX Hi-Fi's bounded input.

Local fixes:
- READ TOC allocation includes the two-byte length field; validate its bound.
- Reject truncated CDDA/TOC transfers and incomplete command-block writes.
- Convert libnx USB Result errors to the negative error convention used by SCSI.
- Copy only bytes actually transferred from the USB receive buffer.
- Initialize USB handles/buffer, avoid repeating constructor-failure cleanup,
  and use separate event IDs with tracked cleanup for subclasses 0x02 and 0x06.
- A failed optical-device check returns false.
- Rebuild static libraries when vendored source/header files change.
