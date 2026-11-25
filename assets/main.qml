// Tube: native BB10 video client. One screen: the player on top (big, full or a small strip), under it the pages
// of the current tab (each tab keeps its own stack of opened pages), and the tab bar at the bottom.
// Going back: swipe right anywhere outside the picture, or the arrow in a page's title strip.
import bb.cascades 1.3
import bb.system 1.2
import tube 1.0
import "yt.js" as Yt
import "nav.js" as N

Page {
    id: app
    property bool signedIn: false
    property bool started: false
    property string signCode: ""
    property string signUrl: ""
    property string signStatus: ""
    property string tab: "home"
    property int depth: 0                 // pages opened on top of the current tab
    property int rootW: 0
    property int rootH: 0
    property bool settingsOpen: false

    actionBarVisibility: ChromeVisibility.Hidden

    function toast(msg) { sysToast.body = msg; sysToast.show(); }
    function saveSettings() { Yt.request("POST", "/store/settings", JSON.stringify(Yt.settings), function () {}); }

    // ---------- pages ----------
    function rootView(t) { return t === "home" ? homeView : t === "subs" ? subsView : t === "search" ? searchView : libView; }
    function showTop() {
        var tabs = ["home", "subs", "search", "lib"], i, j;
        for (i = 0; i < tabs.length; i++) {
            var st = N.stacks[tabs[i]], cur = tabs[i] === app.tab;
            rootView(tabs[i]).visible = cur && st.length === 0;
            for (j = 0; j < st.length; j++) st[j].visible = cur && j === st.length - 1;
        }
        app.depth = N.stacks[app.tab].length;
    }
    function popView() {
        var st = N.stacks[app.tab];
        if (!st.length) return false;
        var v = st.pop();
        v.visible = false;
        browse.remove(v);
        v.destroy();
        showTop();
        return true;
    }
    function setTab(t) {
        if (player.big) player.minimize();
        if (t === app.tab) { if (!popView()) rootView(t).toTop(); return; }      // the tab again: one step up, then to the top
        app.tab = t;
        showTop();
        if (app.started) rootView(t).ensureLoaded();
    }
    function pushView(v) {
        if (player.big) player.minimize();
        browse.add(v);
        N.stacks[app.tab].push(v);
        showTop();
    }
    function back() {
        if (app.settingsOpen) { settingsSheet.close(); return true; }
        if (player.big) { if (player.full) player.setFull(false); else player.minimize(); return true; }
        if (popView()) return true;
        if (app.tab !== "home") { setTab("home"); return true; }
        return false;
    }
    function openItem(it) {
        if (it.type === "video") { player.load(it); return; }
        var v = feedDef.createObject();
        v.host = app; v.title = it.title || " "; v.showBack = true;
        pushView(v);
        var b = { browseId: it.browseId };
        if (it.params) b.params = it.params;
        v.load("browse", b);
    }
    function navAction(it) {               // an entry of the You tab
        if (it.action === "downloads") {
            var v = dlDef.createObject();
            v.host = app;
            pushView(v);
            refreshDownloads();
        } else if (it.action === "settings") openSettings();
        else if (it.action === "account") { if (app.signedIn) signOutDialog.show(); else signIn(); }
        else openItem(it);
    }
    function openSettings() { settingsPage.refresh(); settingsSheet.open(); }
    function showSearch() { setTab("search"); searchView.focusField(); }
    function reloadAll() {
        homeView.loaded = false; subsView.loaded = false; libView.loaded = false;
        rootView(app.tab).ensureLoaded();
        if (app.tab !== "home") homeView.ensureLoaded();
    }

    // ---------- actions on a video (from a list's press-and-hold menu or the player) ----------
    function itemAction(what, it) {
        if (what === "dl") download(it);
        else if (what === "undl") removeDownload(it.id);
        else if (what === "wl") {
            if (!app.signedIn) { toast("Sign in to save videos."); return; }
            Yt.api("browse/edit_playlist", { playlistId: "WL", actions: [{ action: "ACTION_ADD_VIDEO", addedVideoId: it.id }] }, function (o, st) {
                toast(st === 200 ? "Saved to Watch later" : "Could not save.");
            });
        }
    }
    function download(it) {
        if (!it || !it.id || it.type !== "video") return;
        var d = Yt.dl[it.id];
        if (d && d.state === "done") { toast("Already on the phone."); return; }
        var meta = d && d.meta && d.meta.title ? "{}" : JSON.stringify({ title: it.title || "", channel: it.sub || "", thumb: it.thumb || "", dur: it.dur || "" });
        Yt.request("POST", "/dl/start?v=" + encodeURIComponent(it.id) + "&q=" + Yt.settings.dlQuality, meta, function (st) {
            toast(st === 200 ? "Downloading in " + Yt.settings.dlQuality + "p. See You › Downloads." : "Could not start the download.");
            refreshDownloads();
        });
    }
    function pauseDownload(id) { Yt.request("GET", "/dl/pause?v=" + encodeURIComponent(id), null, function () { refreshDownloads(); }); }
    function removeDownload(id) {
        if (player.open && player.videoId === id && player.offline) player.close();
        Yt.request("GET", "/dl/remove?v=" + encodeURIComponent(id), null, function () { toast("Removed from the phone"); refreshDownloads(); });
    }
    function refreshDownloads() {
        Yt.dlRefresh(function (busy) {
            var tabs = ["home", "subs", "search", "lib"], i, j;
            for (i = 0; i < tabs.length; i++) for (j = 0; j < N.stacks[tabs[i]].length; j++) if (N.stacks[tabs[i]][j].isDownloads) N.stacks[tabs[i]][j].dlChanged();
            if (libView.loaded) libView.refreshNav();
            player.dlChanged();
            if (busy) dlTimer.start(); else dlTimer.stop();
        });
    }

    // ---------- account ----------
    function signIn() {
        app.signCode = ""; app.signUrl = ""; app.signStatus = "Asking for a code…";
        signSheet.open();
        Yt.getJson("/auth/start", function (o) {
            if (!o || o.error) { app.signStatus = (o && o.error) || "No connection."; return; }
            app.signCode = o.user_code; app.signUrl = (o.verification_url || "").replace("https://", "");
            app.signStatus = "Waiting for you to enter the code…";
            pollTimer.interval = Math.max(3, o.interval || 5) * 1000;
            pollTimer.start();
        });
    }
    function pollSignIn() {
        Yt.getJson("/auth/poll", function (o) {
            if (!o || o.state === "pending") return;
            pollTimer.stop();
            if (o.state === "ok") { app.signedIn = true; signSheet.close(); toast("Signed in"); reloadAll(); }
            else app.signStatus = o.error || "Sign-in failed.";
        });
    }
    function signOut() {
        Yt.getJson("/auth/signout", function () { app.signedIn = false; toast("Signed out"); reloadAll(); });
    }
    function start() {
        Yt.getJson("/status", function (o) {
            if (!o) { startTimer.start(); return; }             // the core is still coming up
            app.signedIn = o.signedIn;
            app.started = true;
            homeView.ensureLoaded();
            refreshDownloads();
            if (!o.signedIn) signIn();
            // test hook: data/debug.json {"script": [["open", "<video id>"], ["wait", 5000], ["mini"], ...]} is run once
            Yt.getJson("/store/debug", function (d) {
                if (!d || !d.script || !d.script.length) return;
                Yt.request("POST", "/store/debug", "{}", function () {});
                N.script = d.script; N.scriptAt = 0;
                runScript();
            });
        });
    }

    // ---------- swipe right = back ----------
    function swipe(e) {
        if (e.isDown()) {
            N.sx = e.windowX; N.sy = e.windowY; N.st = Date.now();
            N.track = !(player.big && e.windowY < player.areaH);        // the picture has its own gestures (and the seek bar)
        } else if (e.isUp() && N.track) {
            var dx = e.windowX - N.sx, dy = Math.abs(e.windowY - N.sy);
            N.track = false;
            if (dx > ui.du(20) && dy < dx * 0.45 && Date.now() - N.st < 800) back();
        }
    }

    // ---------- when the app is minimised to a tile ----------
    function nativeCmd(cmd) { nativeHook.objectName = cmd; nativeHook.start(0); }
    function toTile() {
        if (!player.open) return;
        if (!Yt.settings.background) { player.pauseForBackground(); return; }
        player.setControls(false);
        player.covered = true;            // the picture at full width even if it was small
        nativeCmd("front");                  // and over the app's own (frozen) picture, so the tile shows it moving
    }
    function fromTile() {
        nativeCmd("behind");
        player.covered = false;
    }

    // ---------- test steps (only from data/debug.json) ----------
    function runScript() {
        while (N.scriptAt < N.script.length) {
            var s = N.script[N.scriptAt++], c = s[0];
            console.log("debug: " + JSON.stringify(s));
            if (c === "wait") { scriptTimer.interval = s[1]; scriptTimer.start(); return; }
            else if (c === "open") player.load({ type: "video", id: s[1], title: s[2] || "", start: s[3] || 0 });
            else if (c === "mini") player.minimize();
            else if (c === "expand") player.expand();
            else if (c === "full") player.setFull(s[1] !== 0);
            else if (c === "close") player.close();
            else if (c === "controls") player.setControls(s[1] !== 0);
            else if (c === "panel") player.panel = s[1];
            else if (c === "quality") player.setQuality(s[1]);
            else if (c === "seek") player.seekTo(s[1]);
            else if (c === "skip") player.skip(s[1]);
            else if (c === "hold") { if (s[1] === 0) player.holdEnd(); else player.holdStart(); }
            else if (c === "toggle") player.toggle();
            else if (c === "tab") setTab(s[1]);
            else if (c === "browse") openItem({ type: "playlist", browseId: s[1], params: s[2] || "", title: s[3] || s[1] });
            else if (c === "nav") navAction({ action: s[1] });
            else if (c === "back") back();
            else if (c === "search") { setTab("search"); searchView.run(s[1]); }
            else if (c === "dl") download({ type: "video", id: s[1], title: s[2] || s[1], sub: "", thumb: "https://i.ytimg.com/vi/" + s[1] + "/mqdefault.jpg", dur: "" });
            else if (c === "dlpause") pauseDownload(s[1]);
            else if (c === "undl") removeDownload(s[1]);
            else if (c === "set") { Yt.settings[s[1]] = s[2]; saveSettings(); }
            else if (c === "settings") { if (s[1] === 0) settingsSheet.close(); else openSettings(); }
            else if (c === "tile") { if (s[1] === 0) fromTile(); else toTile(); }
            else if (c === "native") nativeCmd(s[1]);
            else if (c === "topic") homeView.selectTopic(s[1] === 0 ? "All" : homeView.chipList[s[1]]);     // a chip by position
            else if (c === "more") homeView.more();
            else if (c === "subs") console.log("debug: subs rows " + subsView.rowCount() + " cards " + subsView.cardCount() + " loading " + subsView.loading + " msg " + JSON.stringify(subsView.message.length > 60 ? subsView.message.substring(0, 60) : subsView.message));
            else if (c === "subsmore") subsView.more();
            else if (c === "home") console.log("debug: home topic " + (homeView.topic === "All" ? "All" : "#" + homeView.chipList.indexOf(homeView.topic)) +
                    " chips " + homeView.chipList.length + " rows " + homeView.rowCount() + " cards " + homeView.cardCount() + " loading " + homeView.loading);
            else if (c === "state") console.log("debug: tab " + app.tab + " depth " + app.depth + " root " + app.rootW + "x" + app.rootH + " player open " + player.open +
                    " big " + player.big + " small " + player.small + " full " + player.full + " playing " + player.playing + " pos " + player.positionMs +
                    " area " + player.areaW + "x" + player.areaH + " video " + player.vidW + "x" + player.vidH + " dl " + player.dlState + " " + player.dlPct +
                    " offline " + player.offline + " settings " + JSON.stringify(Yt.settings) + " downloads " + Yt.dlList.length);
        }
    }

    onCreationCompleted: {
        Yt.core = coreUrl;
        Yt.getJson("/store/resume", function (r) { if (r) Yt.resume = r; });
        Yt.getJson("/store/settings", function (o) {
            if (o) { var k; for (k in o) if (Yt.settings.hasOwnProperty(k)) Yt.settings[k] = o[k]; }
            start();
        });
        Application.thumbnail.connect(app.toTile);
        Application.fullscreen.connect(app.fromTile);
    }

    Menu.definition: MenuDefinition {
        settingsAction: SettingsActionItem { onTriggered: app.openSettings() }
        actions: [
            ActionItem {
                title: app.signedIn ? "Sign out" : "Sign in"
                imageSource: "asset:///icons/account.png"
                onTriggered: { if (app.signedIn) signOutDialog.show(); else app.signIn(); }
            },
            ActionItem {
                title: "Downloads"
                imageSource: "asset:///icons/download.png"
                onTriggered: { app.setTab("lib"); while (app.popView()) ; app.navAction({ action: "downloads" }); }
            }
        ]
    }

    shortcuts: [
        Shortcut { key: "Space"; onTriggered: player.toggle() },
        Shortcut { key: "k"; onTriggered: player.toggle() },
        Shortcut { key: "j"; onTriggered: player.skip(-10000) },
        Shortcut { key: "l"; onTriggered: player.skip(10000) },
        Shortcut { key: "f"; onTriggered: { if (player.open) player.setFull(!player.full); } },
        Shortcut { key: "m"; onTriggered: { if (player.open) { if (player.small) player.expand(); else player.minimize(); } } },
        Shortcut { key: "q"; onTriggered: { if (player.open) player.cycleQuality(); } },
        Shortcut { key: "c"; onTriggered: { if (player.open) player.toggleCaptions(); } },
        Shortcut { key: "n"; onTriggered: { if (player.open) player.playNext(); } },
        Shortcut { key: "i"; onTriggered: { if (player.open) player.rate("LIKE"); } },
        Shortcut { key: "u"; onTriggered: { if (player.open) app.itemAction("wl", player.current()); } },
        Shortcut { key: "d"; onTriggered: { if (player.open) player.downloadTapped(); } },
        Shortcut { key: "s"; onTriggered: app.showSearch() },
        Shortcut { key: "h"; onTriggered: app.setTab("home") },
        Shortcut { key: "y"; onTriggered: app.setTab("lib") },
        Shortcut { key: "b"; onTriggered: app.back() },
        Shortcut { key: "r"; onTriggered: { if (!player.big && app.depth === 0 && app.tab !== "search") { var v = app.rootView(app.tab); v.loaded = false; v.ensureLoaded(); } } }
    ]

    Container {
        id: root
        background: Color.create("#ff0f0f0f")
        onTouchCapture: app.swipe(event)
        attachedObjects: [
            LayoutUpdateHandler { onLayoutFrameChanged: { app.rootW = layoutFrame.width; app.rootH = layoutFrame.height; } }
        ]
        PlayerView {
            id: player
            host: app
            rootW: app.rootW
            rootH: app.rootH
            visible: player.open
            layoutProperties: StackLayoutProperties { spaceQuota: player.big ? 1 : -1 }
        }
        Container {
            id: browse
            visible: !player.big
            layout: DockLayout {}
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            horizontalAlignment: HorizontalAlignment.Fill
            Feed { id: homeView; host: app; rootBrowse: "FEtopics"; home: true }
            Feed { id: subsView; host: app; rootBrowse: "FEsubscriptions"; visible: false }
            SearchView { id: searchView; host: app; visible: false }
            LibraryView { id: libView; host: app; visible: false }
        }
        Container {
            id: tabBar
            visible: !player.big
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            horizontalAlignment: HorizontalAlignment.Fill
            background: Color.create("#ff1a1a1a")
            IconButton {
                icon: "asset:///icons/home.png"; label: "Home"; dim: app.tab !== "home"
                iconSize: ui.du(5); layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                onClicked: app.setTab("home")
            }
            IconButton {
                icon: "asset:///icons/subs.png"; label: "Subscriptions"; dim: app.tab !== "subs"
                iconSize: ui.du(5); layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                onClicked: app.setTab("subs")
            }
            IconButton {
                icon: "asset:///icons/search.png"; label: "Search"; dim: app.tab !== "search"
                iconSize: ui.du(5); layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                onClicked: app.setTab("search")
            }
            IconButton {
                icon: "asset:///icons/account.png"; label: "You"; dim: app.tab !== "lib"
                iconSize: ui.du(5); layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                onClicked: app.setTab("lib")
            }
        }
    }

    attachedObjects: [
        ComponentDefinition { id: feedDef; source: "Feed.qml" },
        ComponentDefinition { id: dlDef; source: "DownloadsView.qml" },
        SystemToast { id: sysToast },
        SystemDialog {
            id: signOutDialog
            title: "Sign out?"
            body: "Home, subscriptions, history and likes need the account. Downloads stay on the phone."
            confirmButton.label: "Sign out"
            onFinished: { if (value === SystemUiResult.ConfirmButtonSelection) app.signOut(); }
        },
        QTimer { id: pollTimer; interval: 5000; onTimeout: app.pollSignIn() },
        QTimer { id: startTimer; interval: 500; singleShot: true; onTimeout: app.start() },
        QTimer { id: dlTimer; interval: 2500; onTimeout: app.refreshDownloads() },
        QTimer { id: scriptTimer; singleShot: true; onTimeout: app.runScript() },
        Sheet {
            id: settingsSheet
            onOpened: app.settingsOpen = true
            onClosed: app.settingsOpen = false
            SettingsPage { id: settingsPage; host: app; onDone: settingsSheet.close() }
        },
        Sheet {
            id: signSheet
            onClosed: pollTimer.stop()
            Page {
                titleBar: TitleBar {
                    title: "Sign in"
                    dismissAction: ActionItem { title: "Close"; onTriggered: signSheet.close() }
                }
                Container {
                    layout: DockLayout {}
                    background: Color.create("#ff0f0f0f")
                    Container {
                        horizontalAlignment: HorizontalAlignment.Center
                        verticalAlignment: VerticalAlignment.Center
                        leftPadding: ui.du(4); rightPadding: ui.du(4)
                        Label {
                            text: "On a phone or computer, open"
                            visible: app.signCode !== ""
                            horizontalAlignment: HorizontalAlignment.Center
                            textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#ffc0c0c0") }
                        }
                        Label {
                            text: app.signUrl
                            visible: app.signCode !== ""
                            horizontalAlignment: HorizontalAlignment.Center
                            textStyle { base: SystemDefaults.TextStyles.TitleText; color: Color.White }
                        }
                        Label {
                            text: "and enter this code"
                            visible: app.signCode !== ""
                            horizontalAlignment: HorizontalAlignment.Center
                            textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#ffc0c0c0") }
                        }
                        Label {
                            text: app.signCode
                            visible: app.signCode !== ""
                            horizontalAlignment: HorizontalAlignment.Center
                            textStyle { base: SystemDefaults.TextStyles.BigText; fontWeight: FontWeight.W500; color: Color.White }
                        }
                        Label {
                            text: app.signStatus
                            multiline: true
                            topMargin: ui.du(4)
                            horizontalAlignment: HorizontalAlignment.Center
                            textStyle { base: SystemDefaults.TextStyles.SubtitleText; color: Color.create("#ffa0a0a0"); textAlign: TextAlign.Center }
                        }
                        Label {
                            text: "You can close this and browse or search without an account. Sign in later under You."
                            multiline: true
                            topMargin: ui.du(4)
                            horizontalAlignment: HorizontalAlignment.Center
                            textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.create("#ff808080"); textAlign: TextAlign.Center }
                        }
                    }
                }
            }
        }
    ]
}
