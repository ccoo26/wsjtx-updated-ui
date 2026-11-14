# Contributing to KF0KYL Digi

Thanks for your interest in KF0KYL Digi! Bug reports, ideas and code are all welcome.

## Overview

KF0KYL Digi is a modified version of [WSJT-X](https://wsjt.sourceforge.io/wsjtx.html) 3.0.2,
maintained by Carson Coordes, KF0KYL. It keeps WSJT-X's decoders, modes and rig control
unchanged and adds interface and convenience features on top: a Windows 11 style theme
and dark mode, the waterfall in the main window, automatic QRZ Logbook upload, and
callsign info for the station you're working. See [MODIFICATIONS.md](MODIFICATIONS.md)
for the full list.

It's a one-person, spare-time project, so please be patient with replies.

## Where to report what

**Problems with KF0KYL Digi** (anything in the list above, installing, the theme, the
waterfall, QRZ upload or lookup, settings, crashes): open an
[Issue here](https://github.com/ccoo26/KF0KYL-Digi/issues). Please **don't** send these to
the WSJT-X team or their groups; they don't support this version.

**Problems that also happen in the official WSJT-X** (decoding, a mode's behaviour, rig
control): those belong to the WSJT-X developers. Check whether the official WSJT-X does the
same thing, and if it does, report it at
[github.com/WSJTX/wsjtx/issues](https://github.com/WSJTX/wsjtx/issues). Fixes made there
will come into KF0KYL Digi with the next update.

When opening an Issue, please include:

- KF0KYL Digi version (Help → About) and your operating system (Windows 11, Raspberry Pi OS 64-bit, ...)
- Your radio and how it's connected, if relevant
- What you did, what you expected, and what happened instead
- Screenshots, and for crashes the log files from **File → Open log directory**

**Never post your QRZ password or Logbook API key** in an Issue or screenshot.

## Ideas and feature requests

Open an Issue describing what you'd like and why it would help on the air. Features that
make everyday operating easier are the best fit; changes to how modes encode or decode
are better proposed to the WSJT-X team.

## Code contributions (pull requests)

1. Fork [ccoo26/KF0KYL-Digi](https://github.com/ccoo26/KF0KYL-Digi) on GitHub.
2. Create a branch, make your change, and push it to your fork.
3. Open a pull request against `master` here, explaining what it does, why, and which
   platforms you tested on.

GitHub Actions builds every push for Windows, Linux x86-64 and Linux ARM64 (Raspberry Pi),
so you can download and test the installers from your own fork's **Actions** tab without
setting up a build environment.

### Guidelines

- **One pull request per change.** Don't bundle unrelated fixes.
- **Leave the decoders, timing and rig control alone** (`lib/`, audio, T/R sequencing,
  Hamlib) unless the change also belongs in WSJT-X. Keeping these identical is what makes
  KF0KYL Digi behave exactly like WSJT-X on the air and lets us take in WSJT-X updates.
- **Keep the name.** Don't reintroduce "WSJT-X" as the program's name in anything users
  see; the WSJT-X [trademark policy](TRADEMARK.md) requires modified versions to use their
  own name. Credit lines such as "based on WSJT-X" are fine.
- **Don't break other programs.** Tools like JTAlert and GridTracker talk to KF0KYL Digi
  over the WSJT-X UDP protocol; keep it compatible.
- **Protect user data.** Never log or display passwords or API keys, and don't change
  where settings are stored without migrating them.
- **Test it.** Build and run on your platform. Changes near decoding should be checked
  with the sample `.wav` files (File → Open).

### Coding conventions

Same as WSJT-X:

- **C++:** Qt style. Classes in `PascalCase`, methods in `camelCase`, header guards as
  `FILENAME_HPP_`, 2-space indentation.
- **Fortran:** traditional style; signal processing lives in `lib/`.
- **Comments:** a short block comment above new classes and functions, inline comments
  where the logic isn't obvious.

## Building from source

The easiest way to build is to fork the repository and let GitHub Actions do it. To build
on your own computer you need CMake 3.16+, Qt 5.12+ (Core, Widgets, Multimedia,
SerialPort, Network, Sql, LinguistTools), FFTW 3 (single precision), Boost, C++ and
Fortran compilers (gcc / gfortran), and Git.

**Debian / Ubuntu / Raspberry Pi OS:**
```bash
sudo apt install build-essential cmake gfortran \
  qtbase5-dev qttools5-dev qtmultimedia5-dev libqt5serialport5-dev \
  libfftw3-dev libboost-all-dev libusb-1.0-0-dev libudev-dev \
  autoconf automake libtool pkg-config texinfo
```

**Windows:** use [MSYS2](https://www.msys2.org) with the **UCRT64** environment (the older
MINGW64 environment is being retired by MSYS2). `.github/workflows/build-windows.yml`
lists the exact packages and steps.

### 1. Build Hamlib

Use the Hamlib version that `ci.yml` in `.github/workflows/` builds against
(`hamlib_branch`, currently 4.7.1).

```bash
HAMLIB_BRANCH="4.7.1"
mkdir -p ~/hamlib-prefix/build && cd ~/hamlib-prefix
git clone https://github.com/Hamlib/Hamlib src
cd src && git checkout $HAMLIB_BRANCH && ./bootstrap
cd ../build
../src/configure --prefix=$HOME/hamlib-prefix \
  --disable-shared --enable-static \
  --without-cxx-binding --disable-winradio \
  CFLAGS="-g -O2 -fdata-sections -ffunction-sections -include assert.h" \
  LDFLAGS="-Wl,--gc-sections"
make && make install-strip
```

### 2. Build KF0KYL Digi

```bash
git clone https://github.com/YOUR_USERNAME/KF0KYL-Digi.git ~/kf0kyldigi/src
mkdir -p ~/kf0kyldigi/build && cd ~/kf0kyldigi/build
cmake -DCMAKE_PREFIX_PATH="$HOME/hamlib-prefix" ../src
cmake --build . -j$(nproc)
./wsjtx
```

Internal file names (`wsjtx`, `jt9`, ...) are the same as WSJT-X's on purpose; the program
itself runs as KF0KYL Digi.

## Licence

KF0KYL Digi, like WSJT-X, is licensed under the GNU General Public License v3.0 (see
[COPYING](COPYING)). By contributing you agree that your contribution is licensed under
the same terms.

73 de KF0KYL
