# KF0KYL Digi

KF0KYL Digi is a modified version of **WSJT-X 3.0.2** by Joe Taylor, K1JT, and
the WSJT Development Team. It is maintained by Carson Coordes, KF0KYL, and is
**not** the official WSJT-X or endorsed by its authors. Please send problem
reports about this version to KF0KYL, not to the WSJT-X support groups.

Like WSJT-X, KF0KYL Digi is free software licensed under the GNU General
Public License, version 3 (see [COPYING](COPYING)). Its complete source code
is available at https://github.com/ccoo26/wsjtx-updated-ui. In line with the
WSJT trademark policy ([TRADEMARK.md](TRADEMARK.md)), it uses its own name,
icon and version numbers.

## Changes from WSJT-X 3.0.2

| Date | Change |
|------|--------|
| 2026-11-11 | Windows 11 style light and dark themes (Fusion style, Fluent colours, dark title bar) |
| 2026-11-11 | Waterfall embedded in the main window, with an option to detach it |
| 2026-11-11 | Windows build moved from MSYS2 MINGW64 to UCRT64; Hamlib build fix |
| 2026-11-12 | Automatic upload of logged QSOs to the QRZ.com Logbook, with duplicate protection and offline queue |
| 2026-11-13 | Callsign info strip: name, location, photo and QRZ page link for the DX station (QRZ.com XML, FCC data for US calls) |
| 2026-11-13 | Renamed to KF0KYL Digi v1.0.0 with the Coordes Designs icon; settings and log are copied from WSJT-X on first run |

The full history of every change is in the Git commit log.

## Internal names

Program files keep their WSJT-X names (`wsjtx`, `jt9`, `wsprd`, ...) so the
build and packaging scripts stay compatible. Programs that talk to it over
UDP (JTAlert, GridTracker, loggers) see the name "KF0KYL Digi" with the
WSJT-X version it is based on (3.0.2), so their compatibility checks work.
