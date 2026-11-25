<p align="center">
  <img src=".github/icon.png" width="96" height="96" alt="Tube icon">
</p>

<h1 align="center">Tube</h1>

<p align="center">
  A native YouTube client for BlackBerry 10.<br>
  720p and 1080p playback, downloads and a mini player, without the Android runtime.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/BlackBerry_10-10.3-222222" alt="BlackBerry 10.3">
  <img src="https://img.shields.io/badge/version-0.3.2.0-2f80ed" alt="Version 0.3.2.0">
  <img src="https://img.shields.io/badge/license-MIT-34d399" alt="MIT licence">
</p>

---

Tube is a Cascades app with a small C core that talks to YouTube directly and hands the video to BlackBerry 10's own media player. Because it runs natively, playback is light on the battery: on a Passport it measured about 1.35 W at 720p and 1.56 W at 1080p, against roughly 3.9 W for the Android YouTube app on the same phone.

## Features

- Home feed of full-width cards with topic chips, plus Subscriptions, Search and a Library tab.
- Library: history, Watch later, playlists, your videos and downloads.
- A player that shrinks to a mini player strip while you browse; drag it down to minimise.
- On-picture controls: seek 10 seconds, quality, speed, captions, fullscreen.
- 360p, 480p, 720p and 1080p H.264 with AAC audio.
- Downloads for offline playback, resumable, at a quality you pick.
- Resume positions, autoplay of the next video, background audio when the app is minimised.
- Like, dislike, subscribe, share and save to Watch later when signed in.
- Sign in the way a smart TV does: the app shows a short code, and you approve it on another phone or computer.

## Install

Download `org.bq.tube.bar` from the [latest release](https://github.com/iroham/bb10-tube/releases/latest).

The package is not signed by BlackBerry, so it installs through the system installer running as root. You need a BlackBerry 10.3 device with root SSH access.

```bash
mkdir -p build && cp ~/Downloads/org.bq.tube.bar build/
BB10_HOST=root@192.0.2.10 ./install.sh
```

`install.sh` copies the package to `/var/nt/work` on the device, writes an installer job and prints the result. It calls `grep` and `sleep` from `/var/nt/bin`; change `B=` in the script if your tools live elsewhere. `BB10_HOST` defaults to an SSH alias named `passport`.

## How it works

The app is one process with two halves. The C core (`core/`) runs a local HTTP server on 127.0.0.1. It signs in with YouTube's TV device-code flow, requests the player data, fetches media over YouTube's server-driven streaming protocol (SABR), and repackages the fragmented MP4 it receives into HLS with MPEG-TS segments without re-encoding. BlackBerry 10's media player plays that HLS stream like any other. The QML interface (`assets/`) gets feeds and search results from the core and parses them in `assets/yt.js`.

YouTube's players scramble stream URLs with a JavaScript transform. Tube runs the player script once per player version through QuickJS with yt-dlp's EJS solver (`js/`), caches the result, and then needs about 2.5 seconds per video start.

| Path | Contents |
| --- | --- |
| `core/` | C: networking (libcurl), sign-in and API calls, SABR, HLS packaging, downloads, local server |
| `src/` | C++ launcher that starts the core and the Cascades UI |
| `assets/` | QML interface, feed parsing (`yt.js`) and icons |
| `js/` | QuickJS runner and the glue around the EJS solver |
| `tools/` | Icon generator and an offline test for `yt.js` |
| `package.py` | Builds the `.bar` archive and its manifest |

## Building from source

You need the BlackBerry 10 Native SDK (QNX toolchain, Qt 4.8, Cascades) and two third-party sources placed under `third_party/` (not included here):

| Directory | Source | Licence |
| --- | --- | --- |
| `third_party/quickjs` | [quickjs-ng](https://github.com/quickjs-ng/quickjs) 0.18.0 | MIT |
| `third_party/ejs` | [yt-dlp EJS](https://github.com/yt-dlp/ejs) 0.8.0 (`yt.solver.lib.min.js`, `yt.solver.core.js`) | Unlicense |

```bash
./build.sh               # app and core  -> build/tube
js/build.sh              # QuickJS runner -> build/jsrun
python3 package.py       # package       -> build/org.bq.tube.bar
./install.sh
```

The build scripts target a specific Docker setup: Docker context `colima-aosp`, image `a11-build`, the clang toolchain in volume `a11-src`, and this repository checked out as `native-tube/` next to a `phase6/` directory holding the SDK (`phase6/qnxsdk`) and toolchain environment. Edit the `docker run` lines in [`build.sh`](build.sh) and [`js/build.sh`](js/build.sh) for your own setup.

## Privacy

Your sign-in token is kept in the app's private data folder on the phone. Tube talks only to YouTube and Google's sign-in service. It has no analytics.

The sign-in uses the public OAuth client of YouTube on TV, the same client other open-source YouTube apps use. It is not a private credential.

## Disclaimer

Tube is an unofficial client. It is not affiliated with or endorsed by YouTube or Google. Use it in line with YouTube's Terms of Service.

## Licence

MIT. See [LICENSE](LICENSE). QuickJS and the EJS solver keep their own licences.
