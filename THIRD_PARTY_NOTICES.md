# Third-party notices

EGTRAIN is licensed under the GNU General Public License, version 3 only (see `LICENSE`). The release packages bundle the libraries below, each under its own licence. They are built with Qt 6.8. The Linux AppImage also holds the copyright file of every system library under `usr/share/doc/<package>/copyright`. The table says where the source of each library is available.

| Library | macOS | Windows | Linux | Licence | Source |
| --- | :-: | :-: | :-: | --- | --- |
| Qt 6.8 Core, Gui, Widgets, Network, Svg, OpenGL, OpenGL Widgets and plugins | yes | yes | yes | LGPL-3.0 | https://download.qt.io/archive/qt/6.8/ |
| Qt 6.8 Help and Sql with its SQLite plugin, for the manual | yes | yes | yes | LGPL-3.0 | https://download.qt.io/archive/qt/6.8/ |
| SQLite, inside the Qt SQLite plugin | yes | yes | yes | public domain | https://sqlite.org |
| Qt 6.8 DBus (macOS, Linux) | yes | | yes | LGPL-3.0 | https://download.qt.io/archive/qt/6.8/ |
| Qt Charts 6.8 | yes | yes | yes | GPL-3.0 | https://download.qt.io/archive/qt/6.8/ |
| ZeroMQ (libzmq) | yes | yes | yes | MPL-2.0 | https://github.com/zeromq/libzmq |
| libsodium | yes | | yes | ISC | https://github.com/jedisct1/libsodium |
| OpenPGM (libpgm) | | | yes | LGPL-2.1 | https://github.com/steve-o/openpgm |
| NORM (libnorm) | | | yes | BSD | https://github.com/USNavalResearchLaboratory/norm |
| Microsoft Visual C++ runtime (vcruntime140, msvcp140) | | yes | | Microsoft redistributable terms | no source available |
| libpng | | | yes | libpng License | http://www.libpng.org/pub/png/libpng.html |
| GLib (libglib, libgio, libgobject, libgmodule, libgthread) | | | yes | LGPL-2.1 or later | https://gitlab.gnome.org/GNOME/glib |
| PCRE2 | | | yes | BSD-3-Clause | https://github.com/PCRE2Project/pcre2 |
| Brotli | | | yes | MIT | https://github.com/google/brotli |
| Zstandard | | | yes | BSD-3-Clause or GPL-2.0 | https://github.com/facebook/zstd |
| ICU | | | yes | ICU License (MIT style) | https://icu.unicode.org |
| libxcb with its utility libraries (libxcb-cursor among them), libXau, libXdmcp, libxkbcommon | | | yes | MIT | https://xcb.freedesktop.org, https://xkbcommon.org |
| D-Bus | | | yes | AFL-2.1 or GPL-2.0 or later | https://www.freedesktop.org/wiki/Software/dbus/ |
| libsystemd, libgcrypt, libkeyutils | | | yes | LGPL-2.1 or later (libkeyutils LGPL-2 or later) | Ubuntu 24.04 source packages `systemd`, `libgcrypt20`, `keyutils` |
| MIT Kerberos (libkrb5, libgssapi_krb5, libk5crypto, libkrb5support) | | | yes | MIT style | https://web.mit.edu/kerberos/ |
| libcap | | | yes | BSD-3-Clause or GPL-2.0 | https://sites.google.com/site/fullycapable/ |
| libffi | | | yes | MIT | https://github.com/libffi/libffi |
| libmount, libblkid | | | yes | LGPL-2.1 or later | Ubuntu 24.04 source package `util-linux` |
| libselinux | | | yes | public domain | Ubuntu 24.04 source package `libselinux` |
| xz (liblzma), LZ4, bzip2, libbsd, libmd | | | yes | public domain, BSD-2-Clause, bzip2 licence, BSD style | Ubuntu 24.04 source packages `xz-utils`, `lz4`, `bzip2`, `libbsd`, `libmd` |
| miniz | yes | yes | yes | MIT | https://github.com/richgel999/miniz |
| nlohmann/json | yes | yes | yes | MIT | https://github.com/nlohmann/json |
| cppzmq | yes | yes | yes | MIT | https://github.com/zeromq/cppzmq |

miniz is compiled into the program from `EGTRAIN/QEGTRAIN/io/third_party`. nlohmann/json and cppzmq are header-only libraries compiled into the program. The Qt libraries also contain Qt's own copies of third-party code such as PCRE2, double-conversion and HarfBuzz, listed at https://doc.qt.io/qt-6/licenses-used-in-qt.html.

The case studies have their own sources and terms, listed in `DATA.md` of the source repository, https://github.com/Ancientkingg/EGTRAIN.
