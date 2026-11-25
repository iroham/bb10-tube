// PlayerView: the player. It lives at the top of the main screen for the whole run of the app, in one of three
// shapes: big (picture, action row, details and related videos), full (picture only) and small (a strip with a
// little picture, title, play/pause and close, while the rest of the app is used).
// On the picture: tap shows or hides the controls; double tap on the left / right third skips 10 s back / forward,
// in the middle switches full view; press and hold winds forward (left third: back) until released;
// swipe down makes it small, swipe up makes it full.
// Keys (see main.qml): Space/K play-pause, J/L -10/+10 s, F full, M small, N next, C captions, Q quality,
// I like, U watch later, D download.
import bb.cascades 1.3
import bb.multimedia 1.0
import tube 1.0
import "yt.js" as Yt
import "state.js" as S

Container {
    id: pv
    property variant host
    property int rootW: 0                 // the main screen's size, from main.qml
    property int rootH: 0

    property bool open: false             // a video is loaded
    property bool mini: false             // the user made it small
    property bool covered: false          // the app is minimised to a tile: always show the picture
    property bool full: false
    property bool small: mini && !covered
    property bool big: open && !small
    property bool showCtl: false
    property string panel: ""             // "" or "quality"
    property string badge: ""             // short note over the picture: "+10 s", "2x"

    property string videoId: ""
    property string videoTitle: ""
    property string channelName: ""
    property bool hasStream: false        // S.play is set (the stream is open)
    property bool offline: false          // playing a downloaded copy
    property bool playing: false
    property int playHeight: 0
    property string heights: ""           // ",1080,720,360," what this video offers
    property bool captionsOn: false
    property string captionText: ""
    property int quality: 720
    property int serial: 0
    property bool wantPlay: true          // the user's intent, kept across buffering and quality changes
    property int resumeMs: 0
    property int durationMs: 0
    property int positionMs: 0
    property real aspect: 16 / 9
    property int areaW: 0                 // the black box the picture sits in
    property int areaH: 0
    property int vidW: 0                  // the picture itself
    property int vidH: 0
    property string status: ""
    property bool busy: false
    property string likeState: "INDIFFERENT"
    property bool subscribed: false
    property bool expanded: false
    property string cpn: ""
    property int lastPingS: 0
    property int tick: 0
    property string dlState: ""           // "", "queued", "running", "done", "failed", "paused"
    property int dlPct: 0

    // ---------- loading ----------
    function load(item) {
        var my = ++pv.serial, d = Yt.dl[item.id];
        stopPlayback();
        pv.open = true; pv.mini = false;
        pv.videoId = item.id; pv.videoTitle = item.title || "";
        S.info = null; S.play = null; S.captions = null; pv.hasStream = false; pv.playHeight = 0; pv.heights = "";
        pv.channelName = item.dl ? (item.sub || "") : ""; pv.captionText = "";
        pv.likeState = "INDIFFERENT"; pv.subscribed = false; pv.expanded = false; pv.panel = ""; pv.badge = "";
        pv.durationMs = 0; pv.positionMs = 0;
        pv.resumeMs = (item.start || Yt.resumeGet(item.id) || 0) * 1000;
        pv.wantPlay = true;
        pv.quality = Yt.settings.quality; pv.captionsOn = Yt.settings.captions;
        pv.offline = !!(d && d.state === "done");
        dlChanged();
        related.setRows([{ type: "meta", title: pv.videoTitle, line: item.sub || "", desc: "", expanded: false }]);
        setControls(false);
        startStream(my);
        Yt.api("next", { videoId: pv.videoId }, function (o) {
            if (my !== pv.serial || !o) return;
            var w = Yt.parseWatch(o);
            S.info = w; pv.channelName = w.channel;
            if (w.title) pv.videoTitle = w.title;
            pv.likeState = w.like; pv.subscribed = w.subscribed;
            fillList();
            nowPlaying();
        });
    }
    function fillList() {
        var w = S.info, rows = [], line = [];
        if (!w) return;
        if (w.views) line.push(w.views);
        if (w.date) line.push(w.date);
        if (w.likes) line.push(w.likes + " likes");
        rows.push({ type: "meta", title: pv.videoTitle, line: line.join(" · "), desc: w.description, expanded: pv.expanded });
        if (w.channel) rows.push({ type: "channel", id: "", browseId: w.channelId, params: "", title: w.channel, sub: w.subs,
                                   sub2: pv.subscribed ? "Subscribed" : "", thumb: w.channelThumb, dur: "", pct: 0, short: false });
        rows = rows.concat(Yt.toRows({ sections: w.sections }, 0));
        related.setRows(rows);
    }
    function opened(o) {                   // the stream (from the service or from the phone) is ready
        var i, hs = o.heights || [o.height], list = ",";
        S.play = o; pv.hasStream = true; pv.playHeight = o.height || 0;
        for (i = 0; i < hs.length; i++) list += hs[i] + ",";
        pv.heights = list;
        console.log("watch: stream ready, " + o.height + "p, " + o.width + "x" + o.height_px + ", " + o.duration + " s" + (pv.offline ? ", from the phone" : ""));
        if (o.width > 0 && o.height_px > 0) pv.aspect = o.width / o.height_px;
        pv.durationMs = (o.duration || 0) * 1000;
        pv.cpn = Yt.randomId(16); pv.lastPingS = 0;
        player.sourceUrl = o.hls;
        if (player.prepare() !== MediaError.None) { pv.busy = false; pv.status = "The player could not open the stream."; return; }
        if (pv.resumeMs > 5000 && (!pv.durationMs || pv.resumeMs < pv.durationMs - 10000)) player.seekTime(pv.resumeMs);
        pv.resumeMs = 0;
        if (pv.wantPlay) player.play();
        if (pv.captionsOn) loadCaptions();
        ping(true);
        nowPlaying();
    }
    function startStream(my) {
        pv.busy = true; pv.status = "";
        if (pv.offline) {
            var d = Yt.dl[pv.videoId];
            opened({ status: "OK", hls: Yt.core + "/dlf/" + pv.videoId + "/v.m3u8", height: d.height, heights: [d.height], width: d.w, height_px: d.h, duration: d.dur });
            return;
        }
        Yt.getJson("/play?v=" + encodeURIComponent(pv.videoId) + "&q=" + pv.quality, function (o, st) {
            if (my !== pv.serial) { if (!pv.open) Yt.request("GET", "/stop", null, function () {}); return; }      // closed meanwhile
            if (!o) { pv.busy = false; pv.status = "The player did not answer."; return; }
            if (o.status !== "OK") {
                console.log("watch: not playable: " + o.status);
                pv.busy = false;
                pv.status = o.reason || "This video cannot be played.";
                if (o.status === "LOGIN_REQUIRED" && pv.host && !pv.host.signedIn) pv.status = "Sign in to play videos.";
                return;
            }
            opened(o);
        });
    }
    function stopPlayback() {
        ticker.stop(); hideTimer.stop(); holdTimer.stop(); stepTimer.stop();
        if (pv.hasStream) { ping(false); Yt.resumeSet(pv.videoId, pv.positionMs / 1000, pv.durationMs / 1000); }
        pv.hasStream = false; pv.playing = false;
        player.stop();
        player.reset();
        pv.busy = false;
    }
    function close() {
        pv.serial++;
        stopPlayback();
        npc.revoke();
        Yt.request("GET", "/stop", null, function () {});
        Application.mainWindow.screenIdleMode = 0;
        pv.open = false; pv.mini = false; pv.full = false; pv.panel = ""; pv.videoId = "";
    }

    // ---------- shape ----------
    function minimize() { pv.full = false; pv.panel = ""; pv.mini = true; setControls(false); }
    function expand() { pv.mini = false; }
    function setFull(on) { if (pv.small) pv.mini = false; pv.full = on; pv.panel = ""; }
    function setControls(on) {
        pv.showCtl = on;
        hideTimer.stop();
        if (on && pv.playing) hideTimer.start();
    }
    function layoutVideo() {
        if (!pv.rootW) return;
        var aw, ah, w, h;
        if (pv.small) { ah = Math.round(ui.du(15)); aw = Math.round(ah * 16 / 9); }
        else if (pv.full) { aw = pv.rootW; ah = pv.rootH; }
        else {
            aw = pv.rootW;
            ah = Math.min(Math.round(aw / pv.aspect), Math.floor(pv.rootH * 0.56));
            if (ah < Math.round(aw * 0.40)) ah = Math.round(aw * 0.40);       // room for the controls on very wide films
        }
        w = aw; h = Math.round(w / pv.aspect);
        if (h > ah) { h = ah; w = Math.round(h * pv.aspect); }
        pv.areaW = aw; pv.areaH = ah; pv.vidW = w; pv.vidH = h;
    }
    onAspectChanged: layoutVideo()
    onFullChanged: layoutVideo()
    onSmallChanged: layoutVideo()
    onRootWChanged: layoutVideo()
    onRootHChanged: layoutVideo()

    // ---------- touches on the picture ----------
    function zoneOf(x) { return x < pv.areaW * 0.33 ? -1 : x > pv.areaW * 0.67 ? 1 : 0; }
    function touch(e) {
        if (e.isDown()) {
            S.tx = e.windowX; S.ty = e.windowY; S.lx = e.localX; S.moved = false; S.held = false; S.drag = false;
            if (!pv.small && pv.hasStream) holdTimer.start();
        } else if (e.isMove()) {
            if (!S.moved && (Math.abs(e.windowX - S.tx) > ui.du(3) || Math.abs(e.windowY - S.ty) > ui.du(3))) {
                S.moved = true; holdTimer.stop();
                var ddy = e.windowY - S.ty;              // a downward drag of the open player: it follows the finger
                S.drag = ddy > 0 && Math.abs(ddy) > Math.abs(e.windowX - S.tx) * 1.5 && !pv.small && !pv.full;
                if (S.drag) { setControls(false); pv.panel = ""; }
            }
            if (S.drag) { var fy = Math.max(0, e.windowY - S.ty); pv.translationY = fy; pv.opacity = Math.max(0.35, 1 - fy / pv.rootH); }
        } else if (e.isUp() || e.isCancel()) {
            holdTimer.stop();
            if (S.held) { holdEnd(); return; }
            if (e.isCancel() && !S.drag) return;
            var dx = e.windowX - S.tx, dy = e.windowY - S.ty;
            if (S.drag) {                                  // past a quarter of the screen: minimize, else spring back
                S.drag = false;
                if (!e.isCancel() && dy > pv.rootH * 0.25) { pv.translationY = 0; pv.opacity = 1; minimize(); }
                else { dragBack.play(); }
                return;
            }
            if (S.moved) {
                if (Math.abs(dy) > ui.du(7) && Math.abs(dy) > Math.abs(dx) * 1.5) {
                    if (dy > 0) { if (pv.full) setFull(false); else if (!pv.small) minimize(); }
                    else { if (pv.small) expand(); else if (!pv.full) setFull(true); }
                }
                return;
            }
            if (pv.small) { expand(); return; }
            if (pv.panel !== "") { pv.panel = ""; return; }
            var now = Date.now(), zone = zoneOf(S.lx);
            if (now - S.lastTap < 350 && zone === S.lastZone) { tapTimer.stop(); S.lastTap = 0; doubleTap(zone); }
            else { S.lastTap = now; S.lastZone = zone; tapTimer.start(); }
        }
    }
    function doubleTap(zone) {
        if (zone === 0) { setFull(!pv.full); return; }
        skip(zone * 10000);
    }
    function skip(ms) {                    // +-10 s; repeated skips add up in the note
        if (!pv.hasStream) return;
        if (badgeTimer.active && S.skipSum * ms > 0) S.skipSum += ms; else S.skipSum = ms;
        seekBy(ms);
        note((S.skipSum > 0 ? "+" : "−") + Math.abs(S.skipSum / 1000) + " s");
    }
    function note(text) { pv.badge = text; badgeTimer.start(); }
    // press and hold: wind forward (left third: back) in 10 s steps, faster the longer it is held; the picture
    // follows every step and playback goes on from there when the finger lifts
    function holdStart() {
        if (!pv.hasStream || !pv.durationMs) return;
        S.held = true; S.holdDir = zoneOf(S.lx) < 0 ? -1 : 1; S.holdN = 0; S.holdTo = pv.positionMs;
        badgeTimer.stop();
        holdStep();
        stepTimer.start();
    }
    function holdStep() {
        S.holdN++;
        var step = S.holdN > 12 ? 30000 : S.holdN > 5 ? 20000 : 10000;
        S.holdTo = Math.max(0, Math.min(pv.durationMs - 2000, S.holdTo + S.holdDir * step));
        pv.badge = (S.holdDir > 0 ? "Forward  " : "Back  ") + Yt.fmtTime(S.holdTo);
        seekTo(S.holdTo);
    }
    function holdEnd() {
        S.held = false;
        stepTimer.stop();
        badgeTimer.start();
    }

    // ---------- controls ----------
    function toggle() {
        if (!pv.hasStream) return;
        if (pv.playing) { pv.wantPlay = false; player.pause(); }
        else { pv.wantPlay = true; player.play(); }
    }
    function pauseForBackground() { if (pv.playing) { pv.wantPlay = false; player.pause(); } }
    function seekBy(ms) { seekTo(pv.positionMs + ms); }
    function seekTo(ms) {
        if (!pv.hasStream || !pv.durationMs) return;
        if (ms < 0) ms = 0;
        if (ms > pv.durationMs - 1000) ms = pv.durationMs - 1000;
        pv.positionMs = ms;
        var t0 = Date.now();
        player.seekTime(ms);
        console.log("watch: seek to " + ms + " took " + (Date.now() - t0) + " ms");
    }
    function setQuality(q) {
        pv.panel = "";
        if (pv.offline || (q === pv.playHeight && pv.hasStream)) return;
        pv.quality = q; Yt.settings.quality = q; pv.host.saveSettings();
        if (!pv.videoId) return;
        var pos = pv.positionMs, my = ++pv.serial;
        stopPlayback();
        pv.resumeMs = pos > 5000 ? pos : 0;
        startStream(my);
        pv.host.toast("Quality: " + q + "p");
    }
    function cycleQuality() {
        var all = [1080, 720, 480, 360], opts = [], i;
        for (i = 0; i < all.length; i++) if (pv.heights.indexOf("," + all[i] + ",") >= 0) opts.push(all[i]);
        if (opts.length < 2) return;
        setQuality(opts[(opts.indexOf(pv.playHeight) + 1) % opts.length]);
    }
    function toggleCaptions() {
        pv.captionsOn = !pv.captionsOn; Yt.settings.captions = pv.captionsOn; pv.host.saveSettings();
        pv.captionText = "";
        if (pv.captionsOn && !S.captions) loadCaptions();
        var has = S.play && S.play.captions && S.play.captions.length;
        pv.host.toast(!pv.captionsOn ? "Captions off" : has ? "Captions on" : "This video has no captions");
    }
    function loadCaptions() {
        var tracks = S.play && S.play.captions, my = pv.serial, i, pick = null;
        if (!tracks || !tracks.length) { pv.captionText = ""; return; }
        for (i = 0; i < tracks.length && !pick; i++) if (tracks[i].languageCode === "en" && tracks[i].kind !== "asr") pick = tracks[i];
        for (i = 0; i < tracks.length && !pick; i++) if (tracks[i].languageCode === "en") pick = tracks[i];
        if (!pick) pick = tracks[0];
        Yt.fetch(pick.baseUrl + "&fmt=json3", false, function (t) {
            if (my !== pv.serial || !t) return;
            S.captions = Yt.parseCaptions(t);
        });
    }
    function rate(kind) {                  // "LIKE" | "DISLIKE": pressing the current one clears it
        if (!pv.videoId) return;
        if (!pv.host.signedIn) { pv.host.toast("Sign in to rate videos."); return; }
        var target = pv.likeState === kind ? "INDIFFERENT" : kind;
        var ep = target === "LIKE" ? "like/like" : target === "DISLIKE" ? "like/dislike" : "like/removelike", my = pv.serial;
        Yt.api(ep, { target: { videoId: pv.videoId } }, function (o, st) {
            if (my !== pv.serial) return;
            if (st === 200) { pv.likeState = target; pv.host.toast(target === "LIKE" ? "Liked" : target === "DISLIKE" ? "Disliked" : "Rating removed"); }
            else pv.host.toast("Could not save the rating.");
        });
    }
    function toggleSubscribe() {
        var w = S.info, my = pv.serial;
        if (!w || !w.channelId) return;
        if (!pv.host.signedIn) { pv.host.toast("Sign in to subscribe."); return; }
        var want = !pv.subscribed;
        Yt.api(want ? "subscription/subscribe" : "subscription/unsubscribe", { channelIds: [w.channelId] }, function (o, st) {
            if (my !== pv.serial) return;
            if (st === 200) { pv.subscribed = want; fillList(); pv.host.toast(want ? "Subscribed to " + w.channel : "Unsubscribed"); }
            else pv.host.toast("Could not change the subscription.");
        });
    }
    function current() {                   // the playing video as a list item (for download / watch later)
        var w = S.info;
        return { type: "video", id: pv.videoId, title: pv.videoTitle, sub: pv.channelName, thumb: "https://i.ytimg.com/vi/" + pv.videoId + "/mqdefault.jpg",
                 dur: pv.durationMs ? Yt.fmtTime(pv.durationMs) : "" };
    }
    function downloadTapped() {
        if (!pv.videoId) return;
        if (pv.dlState === "done") pv.host.toast("Already on the phone. Remove it under You › Downloads.");
        else if (pv.dlState === "running" || pv.dlState === "queued") pv.host.pauseDownload(pv.videoId);
        else pv.host.download(current());
    }
    function dlChanged() {                 // main.qml read the download table again
        var d = Yt.dl[pv.videoId];
        pv.dlState = d ? d.state : "";
        pv.dlPct = d && d.nseg > 0 ? Math.floor(d.done * 100 / d.nseg) : 0;
    }
    function playNext() {
        var w = S.info, i, j;
        if (w && w.next) { var keep = pv.mini; load({ id: w.next, title: "" }); pv.mini = keep; return; }
        if (w) for (i = 0; i < w.sections.length; i++) for (j = 0; j < w.sections[i].items.length; j++)
            if (w.sections[i].items[j].type === "video") { var k = pv.mini; load(w.sections[i].items[j]); pv.mini = k; return; }
    }
    function share() {
        if (!pv.videoId) return;
        shareInv.go = true;
        shareInv.query.data = "https://youtu.be/" + pv.videoId;
        shareInv.query.updateQuery();
    }

    // ---------- reporting (watch history, like the TV app does) ----------
    function ping(start) {
        var tr = S.play && S.play.tracking, pos = Math.floor(pv.positionMs / 1000);
        if (!tr || !pv.host.signedIn) return;
        if (start) {
            if (tr.videostatsPlaybackUrl) Yt.fetch(tr.videostatsPlaybackUrl.baseUrl + "&ver=2&cpn=" + pv.cpn + "&cmt=0&el=detailpage", true, function () {});
            return;
        }
        if (pos <= pv.lastPingS || !tr.videostatsWatchtimeUrl) return;
        Yt.fetch(tr.videostatsWatchtimeUrl.baseUrl + "&ver=2&cpn=" + pv.cpn + "&el=detailpage&st=" + pv.lastPingS + "&et=" + pos + "&cmt=" + pos +
                 "&len=" + Math.floor(pv.durationMs / 1000) + "&state=playing", true, function () {});
        pv.lastPingS = pos;
    }
    function nowPlaying() {
        if (!pv.hasStream) return;
        npc.acquire();
        npc.duration = pv.durationMs;
        npc.setMetaData({ "track": pv.videoTitle, "artist": pv.channelName });
    }

    // ================= layout =================
    background: Color.create("#ff0f0f0f")
    horizontalAlignment: HorizontalAlignment.Fill

    Container {
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        horizontalAlignment: HorizontalAlignment.Fill
        background: pv.small ? Color.create("#ff1c1c1c") : Color.Black
        Container {
            id: videoArea
            layout: DockLayout {}
            background: Color.Black
            preferredWidth: pv.areaW; minWidth: pv.areaW; maxWidth: pv.areaW
            preferredHeight: pv.areaH; minHeight: pv.areaH; maxHeight: pv.areaH
            ForeignWindowControl {
                id: video
                objectName: "tubeVideo"
                windowId: "tubeVideo"
                updatedProperties: WindowProperty.Position | WindowProperty.Size | WindowProperty.Visible
                visible: boundToWindow
                preferredWidth: pv.vidW; preferredHeight: pv.vidH
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
            }
            // darkens the picture while the controls are shown
            Container {
                visible: pv.showCtl && !pv.small
                background: Color.create("#70000000")
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
            }
            // the touch surface (buttons below are drawn over it and take their own touches)
            Container {
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Fill
                onTouch: pv.touch(event)
            }
            // captions
            Container {
                visible: pv.captionsOn && pv.captionText !== "" && !pv.small
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Bottom
                bottomPadding: pv.showCtl ? ui.du(12) : ui.du(2); leftPadding: ui.du(3); rightPadding: ui.du(3)
                touchPropagationMode: TouchPropagationMode.None
                Container {
                    background: Color.create("#b0000000")
                    leftPadding: ui.du(1.2); rightPadding: ui.du(1.2); topPadding: ui.du(0.4); bottomPadding: ui.du(0.4)
                    Label {
                        text: pv.captionText
                        multiline: true
                        textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.White; textAlign: TextAlign.Center }
                    }
                }
            }
            // waiting / message
            Container {
                visible: pv.busy || pv.status !== ""
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                leftPadding: ui.du(4); rightPadding: ui.du(4)
                touchPropagationMode: TouchPropagationMode.None
                ActivityIndicator {
                    running: pv.busy
                    visible: pv.busy
                    horizontalAlignment: HorizontalAlignment.Center
                    preferredWidth: pv.small ? ui.du(7) : ui.du(12); preferredHeight: pv.small ? ui.du(7) : ui.du(12)
                }
                Label {
                    text: pv.status
                    visible: pv.status !== "" && !pv.small
                    multiline: true
                    horizontalAlignment: HorizontalAlignment.Center
                    textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.White; textAlign: TextAlign.Center }
                }
            }
            // note ("+10 s", "2x speed")
            Container {
                visible: pv.badge !== "" && !pv.small
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Top
                topPadding: ui.du(11)
                touchPropagationMode: TouchPropagationMode.None
                Container {
                    background: Color.create("#c0000000")
                    leftPadding: ui.du(2); rightPadding: ui.du(2); topPadding: ui.du(0.8); bottomPadding: ui.du(0.8)
                    Label {
                        text: pv.badge
                        textStyle { base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.W500; color: Color.White }
                    }
                }
            }
            // ---- controls: top row ----
            Container {
                visible: pv.showCtl && !pv.small && pv.panel === ""
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Top
                IconButton {
                    icon: "asset:///icons/down.png"
                    onClicked: pv.minimize()
                }
                Label {
                    text: pv.full ? pv.videoTitle : ""
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    textStyle { base: SystemDefaults.TextStyles.SubtitleText; color: Color.White }
                }
                IconButton {
                    icon: "asset:///icons/captions.png"
                    dim: !pv.captionsOn
                    iconSize: ui.du(5.5)
                    onClicked: { pv.toggleCaptions(); pv.setControls(true); }
                }
                Chip {
                    text: (pv.playHeight ? pv.playHeight : pv.quality) + "p"
                    onClicked: { hideTimer.stop(); pv.panel = "quality"; }
                }
            }
            // ---- controls: middle ----
            Container {
                visible: pv.showCtl && !pv.small && pv.panel === "" && !pv.busy && pv.status === ""
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                IconButton {
                    icon: "asset:///icons/back10.png"
                    iconSize: ui.du(8)
                    minWidth: ui.du(16); minHeight: ui.du(14)
                    onClicked: { pv.skip(-10000); pv.setControls(true); }
                }
                IconButton {
                    icon: pv.playing ? "asset:///icons/pause.png" : "asset:///icons/play.png"
                    iconSize: ui.du(11)
                    minWidth: ui.du(20); minHeight: ui.du(14)
                    onClicked: { pv.toggle(); pv.setControls(true); }
                }
                IconButton {
                    icon: "asset:///icons/fwd10.png"
                    iconSize: ui.du(8)
                    minWidth: ui.du(16); minHeight: ui.du(14)
                    onClicked: { pv.skip(10000); pv.setControls(true); }
                }
            }
            // ---- controls: bottom row ----
            Container {
                visible: pv.showCtl && !pv.small && pv.panel === ""
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                horizontalAlignment: HorizontalAlignment.Fill
                verticalAlignment: VerticalAlignment.Bottom
                leftPadding: ui.du(2)
                Label {
                    text: Yt.fmtTime(pv.positionMs) + " / " + Yt.fmtTime(pv.durationMs)
                    verticalAlignment: VerticalAlignment.Center
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.White }
                }
                Slider {
                    id: bar
                    fromValue: 0
                    toValue: pv.durationMs > 0 ? pv.durationMs : 1
                    enabled: pv.durationMs > 0
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    property bool setting: false
                    // the user let go (our own updates and range changes land within a second of the position)
                    onValueChanged: { if (!bar.setting && Math.abs(value - pv.positionMs) > 1500) { pv.seekTo(Math.round(value)); pv.setControls(true); } }
                    onImmediateValueChanged: { if (!bar.setting && pv.showCtl) { hideTimer.stop(); if (pv.playing) hideTimer.start(); } }
                }
                IconButton {
                    icon: pv.full ? "asset:///icons/exitfull.png" : "asset:///icons/fullscreen.png"
                    iconSize: ui.du(5.5)
                    onClicked: pv.setFull(!pv.full)
                }
            }
            // ---- quality / speed panel ----
            Container {
                visible: pv.panel !== "" && !pv.small
                horizontalAlignment: HorizontalAlignment.Center
                verticalAlignment: VerticalAlignment.Center
                background: Color.create("#f0181818")
                topPadding: ui.du(1); bottomPadding: ui.du(1)
                Label {
                    text: pv.offline ? "Quality (downloaded copy)" : "Quality"
                    horizontalAlignment: HorizontalAlignment.Center
                    textStyle { base: SystemDefaults.TextStyles.SubtitleText; color: Color.create("#ffb0b0b0") }
                }
                OptionRow { visible: pv.panel === "quality" && pv.heights.indexOf(",1080,") >= 0; text: "1080p"; checked: pv.playHeight === 1080; onPicked: pv.setQuality(1080) }
                OptionRow { visible: pv.panel === "quality" && pv.heights.indexOf(",720,") >= 0; text: "720p"; checked: pv.playHeight === 720; onPicked: pv.setQuality(720) }
                OptionRow { visible: pv.panel === "quality" && pv.heights.indexOf(",480,") >= 0; text: "480p"; checked: pv.playHeight === 480; onPicked: pv.setQuality(480) }
                OptionRow { visible: pv.panel === "quality" && pv.heights.indexOf(",360,") >= 0; text: "360p"; checked: pv.playHeight === 360; onPicked: pv.setQuality(360) }
            }
            // ---- thin progress line while the controls are hidden ----
            Container {
                visible: !pv.showCtl && pv.durationMs > 0
                horizontalAlignment: HorizontalAlignment.Left
                verticalAlignment: VerticalAlignment.Bottom
                preferredHeight: ui.du(0.5)
                preferredWidth: pv.durationMs > 0 ? pv.areaW * pv.positionMs / pv.durationMs : 0
                background: Color.create("#ffe53935")
                touchPropagationMode: TouchPropagationMode.None
            }
        }
        // ---- the small shape: title and two buttons beside the little picture ----
        Container {
            visible: pv.small
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            verticalAlignment: VerticalAlignment.Fill
            Container {
                layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                verticalAlignment: VerticalAlignment.Center
                leftPadding: ui.du(2)
                gestureHandlers: [ TapHandler { onTapped: pv.expand() } ]
                Label {
                    text: pv.videoTitle
                    multiline: true
                    autoSize.maxLineCount: 2
                    textStyle { base: SystemDefaults.TextStyles.SubtitleText; fontWeight: FontWeight.W500; color: Color.White }
                }
                Label {
                    text: pv.channelName
                    visible: pv.channelName !== ""
                    topMargin: 0
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.create("#ffa0a0a0") }
                }
            }
            IconButton {
                icon: pv.playing ? "asset:///icons/pause.png" : "asset:///icons/play.png"
                verticalAlignment: VerticalAlignment.Center
                minWidth: ui.du(12); minHeight: ui.du(13)
                onClicked: pv.toggle()
            }
            IconButton {
                icon: "asset:///icons/close.png"
                verticalAlignment: VerticalAlignment.Center
                iconSize: ui.du(5)
                minWidth: ui.du(12); minHeight: ui.du(13)
                onClicked: pv.close()
            }
        }
    }
    // ---- big shape: actions, then details and related videos ----
    Container {
        visible: pv.big && !pv.full
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        horizontalAlignment: HorizontalAlignment.Fill
        background: Color.create("#ff161616")
        IconButton {
            icon: "asset:///icons/like.png"; label: pv.likeState === "LIKE" ? "Liked" : "Like"; dim: pv.likeState !== "LIKE"
            iconSize: ui.du(5); layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            onClicked: pv.rate("LIKE")
        }
        IconButton {
            icon: "asset:///icons/dislike.png"; label: pv.likeState === "DISLIKE" ? "Disliked" : "Dislike"; dim: pv.likeState !== "DISLIKE"
            iconSize: ui.du(5); layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            onClicked: pv.rate("DISLIKE")
        }
        IconButton {
            icon: "asset:///icons/subscribe.png"; label: pv.subscribed ? "Subscribed" : "Subscribe"; dim: !pv.subscribed
            iconSize: ui.du(5); layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            onClicked: pv.toggleSubscribe()
        }
        IconButton {
            icon: "asset:///icons/later.png"; label: "Later"; dim: true
            iconSize: ui.du(5); layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            onClicked: pv.host.itemAction("wl", pv.current())
        }
        IconButton {
            icon: pv.dlState === "done" ? "asset:///icons/done.png" : "asset:///icons/download.png"
            label: pv.dlState === "done" ? "On phone" : pv.dlState === "running" ? pv.dlPct + "%" : pv.dlState === "queued" ? "Waiting" : pv.dlState === "paused" ? "Paused" : pv.dlState === "failed" ? "Retry" : "Download"
            dim: pv.dlState === ""
            iconSize: ui.du(5); layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            onClicked: pv.downloadTapped()
        }
        IconButton {
            icon: "asset:///icons/share.png"; label: "Share"; dim: true
            iconSize: ui.du(5); layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            onClicked: pv.share()
        }
    }
    VideoList {
        id: related
        visible: pv.big && !pv.full
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        onPicked: {
            if (item.type === "meta") { pv.expanded = !pv.expanded; item.expanded = pv.expanded; related.model.replace(index, item); }
            else if (item.type === "video") pv.load(item);
            else if (item.type !== "more") pv.host.openItem(item);
        }
        onAction: pv.host.itemAction(what, item)
    }
    // separates the small strip from the page under it
    Container {
        visible: pv.small
        horizontalAlignment: HorizontalAlignment.Fill
        preferredHeight: 2; minHeight: 2
        background: Color.create("#ff3a3a3a")
    }

    animations: [
        ParallelAnimation {
            id: dragBack
            TranslateTransition { toY: 0; duration: 200; easingCurve: StockCurve.CubicOut }
            FadeTransition { toOpacity: 1; duration: 200 }
        }
    ]
    attachedObjects: [
        ImplicitAnimationController { propertyName: "translationY"; enabled: false },
        ImplicitAnimationController { propertyName: "opacity"; enabled: false },
        MediaPlayer {
            id: player
            windowId: video.windowId
            windowGroupId: video.windowGroup
            videoOutput: VideoOutput.PrimaryDisplay
            statusInterval: 500
            onPositionChanged: {
                pv.positionMs = position;
                pv.tick++;
                if (pv.tick % 20 === 1) console.log("watch: pos " + position + " / " + pv.durationMs + " bound " + video.boundToWindow);
                // move the bar unless the user is dragging it
                if (Math.abs(bar.immediateValue - bar.value) < 1) { bar.setting = true; bar.value = position; bar.setting = false; }
                if (pv.captionsOn && S.captions) pv.captionText = Yt.captionAt(S.captions, position);
            }
            onDurationChanged: { if (duration > 0) { pv.durationMs = duration; npc.duration = duration; } }
            onMediaStateChanged: {
                var playing = mediaState === MediaState.Started;
                pv.playing = playing;
                console.log("watch: state " + mediaState + " pos " + pv.positionMs + " area " + pv.areaW + "x" + pv.areaH + " video " + pv.vidW + "x" + pv.vidH + " bound " + video.boundToWindow +
                            (pv.small ? " small" : pv.full ? " full" : " big"));
                if (playing) { pv.busy = false; pv.status = ""; ticker.start(); if (pv.showCtl) hideTimer.start(); }
                else { ticker.stop(); hideTimer.stop(); if (pv.hasStream && !pv.wantPlay && !pv.small) pv.showCtl = true; }
                Application.mainWindow.screenIdleMode = playing ? 1 : 0;      // keep the screen on while playing
                npc.mediaState = mediaState;
            }
            onBufferStatusChanged: pv.busy = (bufferStatus === BufferStatus.Buffering) && pv.wantPlay
            onPlaybackCompleted: {
                pv.ping(false);
                Yt.resumeSet(pv.videoId, 0, 0);
                if (Yt.settings.autoplay && !pv.offline) pv.playNext(); else { pv.wantPlay = false; pv.setControls(true); }
            }
            onError: {
                console.log("player error " + mediaError + " at " + position + " (last known " + pv.positionMs + ")");
                pv.busy = false;
                // the stream address can expire or the session can be asked to reload: start again where we were
                if (pv.videoId && pv.positionMs > 0 && pv.durationMs - pv.positionMs > 3000) {
                    var pos = pv.positionMs, my = ++pv.serial;
                    pv.stopPlayback(); pv.resumeMs = pos; pv.startStream(my);
                } else pv.status = "Playback stopped (error " + mediaError + ").";
            }
        },
        NowPlayingConnection {
            id: npc
            connectionName: "tube"
            iconUrl: "asset:///icons/play.png"
            onPause: { if (pv.hasStream) { pv.wantPlay = false; player.pause(); } }
            onPlay: { if (pv.hasStream) { pv.wantPlay = true; player.play(); } }
            onRevoked: { if (pv.hasStream && pv.playing) { pv.wantPlay = false; player.pause(); } }
            onNext: { if (pv.open) pv.playNext(); }
        },
        Invocation {
            id: shareInv
            property bool go: false
            query { mimeType: "text/plain" }
            onArmed: { if (shareInv.go) { shareInv.go = false; shareInv.trigger("bb.action.SHARE"); } }
        },
        QTimer { id: ticker; interval: 40000; onTimeout: { pv.ping(false); Yt.resumeSet(pv.videoId, pv.positionMs / 1000, pv.durationMs / 1000); } },
        QTimer { id: hideTimer; interval: 3500; singleShot: true; onTimeout: { if (pv.playing && pv.panel === "") pv.showCtl = false; } },
        QTimer { id: tapTimer; interval: 300; singleShot: true; onTimeout: pv.setControls(!pv.showCtl) },
        QTimer { id: holdTimer; interval: 480; singleShot: true; onTimeout: pv.holdStart() },
        QTimer { id: stepTimer; interval: 650; onTimeout: pv.holdStep() },
        QTimer { id: badgeTimer; interval: 900; singleShot: true; onTimeout: pv.badge = "" }
    ]
}
