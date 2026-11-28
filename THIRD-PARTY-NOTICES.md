# Third-party notices

Applies to: LegionGames Moonlight client, including the bundled NetBird client.
Version: 1.0, 2026-11-28.

This file lists the third-party components distributed with the client and the
licence terms that apply to them. The full licence text governs; this summary
does not replace it. Full texts are kept in `LICENSES/` or in the component's
own source distribution.

## 1. Moonlight / LegionGames fork

- Component: Moonlight PC client, LegionGames fork.
- Licence: GNU General Public License, version 3 (GPLv3).
- Copyright: Moonlight Stream Project and contributors; LegionGames
  modifications.
- Licence text: [LICENSE](LICENSE) in the repository root.
- Source: <https://github.com/qtkksd/legiongames-moonlight>

The LegionGames fork is open source. The source revision for a distributed
build is the git commit embedded in that build (`SystemProperties.commitString`)
and recorded by the release pipeline. Use the commit to open the exact source
tree:

`https://github.com/qtkksd/legiongames-moonlight/tree/<commit>`

The complete Corresponding Source for the GPLv3-covered client is the
repository at the embedded commit. The release pipeline records that commit in
the build and in the update manifest, so the binary and the source revision can
be matched.

## 2. NetBird client (LegionGames fork)

- Component: NetBird client (agent/CLI) bundled with the Moonlight installer.
- Displayed client version: `0.79.0-awg.9`.
- Fork tag: `v0.79.0-awg.9`.
- Licence: BSD 3-Clause.
- Copyright: (c) 2022 NetBird GmbH & AUTHORS.
- Licence text: [LICENSES/BSD-3-Clause.txt](LICENSES/BSD-3-Clause.txt).
- Fork source:
  <https://github.com/qtkksd/legiongames-netbird/tree/v0.79.0-awg.9>
- Upstream project: <https://github.com/netbirdio/netbird> and
  <https://netbird.io>

This is a LegionGames fork based on NetBird. It is not an official NetBird
release.

The NetBird client build collects its own licence set with
`client/collect-licenses.sh` in the NetBird fork: `BSD-3-Clause.txt`, the Go
runtime terms and per-module licences under `third_party/`. The installer must
keep that directory next to the bundled NetBird client.

### AmneziaWG modifications

The LegionGames fork adds AmneziaWG client modifications to the NetBird client.
Those modifications are part of the fork identified by tag `v0.79.0-awg.9` and
are distributed under the same BSD 3-Clause terms. The fork source link above
is the exact source for the bundled client revision.

### Components not distributed

The NetBird management, signal, relay, combined and proxy services are
internal-only. They are not part of the Moonlight installer and no source offer
for them is included in the client package.

## 3. Components bundled from this repository

The following components are included in the client source tree and/or the
Windows dependency bundle. Licence statements below were verified against the
licence or header file named in the last column.

| Component | Licence | Verified in |
|---|---|---|
| qmdnsengine | MIT | `qmdnsengine/qmdnsengine/LICENSE.txt` |
| SDL2 | zlib | `libs/windows/include/x64/SDL2/SDL.h` |
| SDL3 | zlib | `libs/windows/include/x64/SDL3/SDL.h` |
| SDL2_ttf | zlib | `libs/windows/include/x64/SDL2/SDL_ttf.h` |
| SDL_GameControllerDB | zlib | `app/SDL_GameControllerDB/LICENSE` |
| moonlight-common-c | GPLv3 | `moonlight-common-c/moonlight-common-c/LICENSE.txt` |
| h264bitstream | LGPL-2.1 | `h264bitstream/h264bitstream/LICENSE` |
| enet | MIT | `moonlight-common-c/moonlight-common-c/enet/LICENSE` |
| nanors | MIT | `moonlight-common-c/moonlight-common-c/nanors/LICENSE` |
| OpenSSL | Apache-2.0 | `libs/windows/include/x64/openssl/opensslv.h` |
| FFmpeg libraries (libavcodec, libavutil, libswscale) | LGPL-2.1-or-later according to the bundled headers | `libs/windows/include/x64/libavutil/avutil.h` |

The SDL headers are the zlib licence and carry the Sam Lantinga copyright.
The SDL_GameControllerDB file carries the same zlib-style terms.

The prebuilt FFmpeg DLLs are configured outside this repository. The packaging
step must confirm the configure flags of the shipped DLLs and include the
matching licence text; the header statement alone does not prove the build
configuration.

## 4. Build and packaging dependencies not covered here

- **Qt**: the build requires Qt (Qt 6.7 or later is documented in `README.md`).
  Qt is supplied by the Qt SDK used for the build, not vendored in this
  repository. The packaging step must include the licence texts for the exact
  Qt edition, version and modules that are deployed with the installer. No Qt
  licence is asserted in this file.
- **Other prebuilt DLLs** in `libs/` (for example dav1d, libplacebo, opus,
  discord-rpc and detours) are governed by their respective upstream licences.
  Their licence texts must be included by the packaging step. No licence is
  asserted for them in this file.

## 5. Reporting

Licence and notice corrections: <rem.legiongames@yandex.com>.
