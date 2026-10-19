# Third-party notices

EGTRAIN is licensed under the GNU General Public License, version 3 only (see `LICENSE`). The release packages bundle the libraries below, each under its own licence. The Linux AppImage also holds the copyright file of every system library under `usr/share/doc/<package>/copyright`. The table says where the source of each library is available.

| Library | macOS | Windows | Linux | Licence | Source |
| --- | :-: | :-: | :-: | --- | --- |
| Qt 5.15 Core, Gui, Widgets, Network, Svg and plugins | yes | yes | yes | LGPL-3.0 | https://download.qt.io/archive/qt/5.15/ |
| Qt 5.15 DBus (macOS, Linux), PrintSupport (macOS) | yes | | yes | LGPL-3.0 | https://download.qt.io/archive/qt/5.15/ |
| Qt Charts 5.15 | yes | yes | yes | GPL-3.0 | https://download.qt.io/archive/qt/5.15/ |
| ZeroMQ (libzmq) | yes | yes | yes | MPL-2.0 | https://github.com/zeromq/libzmq |
| libsodium | yes | | yes | ISC | https://github.com/jedisct1/libsodium |
| OpenPGM (libpgm) | | | yes | LGPL-2.1 | https://github.com/steve-o/openpgm |
| NORM (libnorm) | | | yes | BSD | https://github.com/USNavalResearchLaboratory/norm |
| Microsoft Visual C++ runtime (vcruntime140, msvcp140) | | yes | | Microsoft redistributable terms | no source available |
| FreeType | yes | | | FreeType License or GPL-2.0 | https://freetype.org |
| libpng | yes | | yes | libpng License | http://www.libpng.org/pub/png/libpng.html |
| libjpeg-turbo | yes | | yes | IJG, BSD-3-Clause and zlib | https://libjpeg-turbo.org |
| GLib | yes | | yes | LGPL-2.1 or later | https://gitlab.gnome.org/GNOME/glib |
| GNU gettext libintl | yes | | | LGPL-2.1 or later | https://www.gnu.org/software/gettext/ |
| PCRE2 | yes | | yes | BSD-3-Clause | https://github.com/PCRE2Project/pcre2 |
| md4c | yes | | yes | MIT | https://github.com/mity/md4c |
| Brotli | yes | | yes | MIT | https://github.com/google/brotli |
| Zstandard | yes | | yes | BSD-3-Clause or GPL-2.0 | https://github.com/facebook/zstd |
| double-conversion | | | yes | BSD-3-Clause | https://github.com/google/double-conversion |
| ICU | | | yes | ICU License (MIT style) | https://icu.unicode.org |
| libxcb, libXau, libXdmcp, libXrender, libxkbcommon | | | yes | MIT | https://xcb.freedesktop.org, https://xkbcommon.org |
| D-Bus | | | yes | AFL-2.1 or GPL-2.0 or later | https://www.freedesktop.org/wiki/Software/dbus/ |
| libsystemd, libgcrypt, libkeyutils | | | yes | LGPL-2.1 or later (libkeyutils LGPL-2 or later) | Ubuntu 24.04 source packages `systemd`, `libgcrypt20`, `keyutils` |
| MIT Kerberos (libkrb5, libgssapi_krb5, libk5crypto, libkrb5support) | | | yes | MIT style | https://web.mit.edu/kerberos/ |
| libcap | | | yes | BSD-3-Clause or GPL-2.0 | https://sites.google.com/site/fullycapable/ |
| Graphite2 | | | yes | LGPL-2.1 or later | https://github.com/silnrsi/graphite |
| xz (liblzma), LZ4, bzip2, libbsd, libmd | | | yes | public domain, BSD-2-Clause, bzip2 licence, BSD style | Ubuntu 24.04 source packages `xz-utils`, `lz4`, `bzip2`, `libbsd`, `libmd` |
| miniz | yes | yes | yes | MIT | https://github.com/richgel999/miniz |
| nlohmann/json | yes | yes | yes | MIT | https://github.com/nlohmann/json |
| cppzmq | yes | yes | yes | MIT | https://github.com/zeromq/cppzmq |

miniz is compiled into the program from `EGTRAIN/QEGTRAIN/io/third_party`. nlohmann/json and cppzmq are header-only libraries compiled into the program. The Windows Qt libraries also contain Qt's own copies of third-party code such as PCRE2, double-conversion and HarfBuzz.

The case studies have their own sources and terms, listed in `DATA.md` of the source repository, https://github.com/Ancientkingg/EGTRAIN.
