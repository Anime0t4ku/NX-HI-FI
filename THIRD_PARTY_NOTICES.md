# Third-party components

NX Hi-Fi is GPL-3.0. It uses SDL2, SDL2_image, SDL2_ttf, libnx, libusbdvd, libsmb2, curl/mbedTLS, FFmpeg, dav1d, libpng, libjpeg, FreeType, HarfBuzz, miniaudio, stb_vorbis and nlohmann/json, plus their transitive dependencies.

The Switch NRO links devkitPro's switch-ffmpeg 7.1 package, built with GPL enabled. Refer to the FFmpeg GPL-2.0-or-later terms, the app GPLv3 license, and the upstream source and devkitPro package recipes. The included FFmpeg LGPL notice also covers its LGPL components. Source: https://ffmpeg.org/ and https://github.com/devkitPro/pacman-packages.

Switch rendering uses devkitPro SDL2's accelerated EGL/OpenGL ES path. PS5 AGC/runtime code is not included in the Switch executable.

The Switch uses its shared system font. The optional host test/preview font in assets/font.ttf is Noto CJK, distributed under the SIL Open Font License in licenses/Noto-CJK-OFL.txt. Other bundled notices are in licenses/ and licenses/portlibs/.

Music and user artwork are not included.
