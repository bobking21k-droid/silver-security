# Third-party code and data in SilverOS

| Component | Used for | License |
|-----------|----------|---------|
| BearSSL 0.6 (Thomas Pornin) | TLS in `silver-fetch` | MIT |
| stb_image.h (Sean Barrett) | decoding JPEG thumbnails (`src/common/stb_image.h`, JPEG only) | MIT or public domain |
| Mozilla root certificates, as published at curl.se/ca | trust anchors in `/etc/ssl/cacert.pem` | MPL 2.0 |
| Inter, JetBrains Mono (removed), Michroma, Bowlby One | fonts, see `fonts/LICENSES.md` | SIL OFL 1.1 |
| Linux, musl, BusyBox, Dropbear, util-linux, e2fsprogs | the system itself, built from source | their own licenses |
