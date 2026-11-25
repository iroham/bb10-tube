// LibraryView: the "You" tab. A row of shortcuts (downloads, history, watch later, playlists, own videos,
// settings, account) like the phone apps, then the recently watched videos as cards two to a row.
import bb.cascades 1.3
import "yt.js" as Yt

Container {
    id: lib
    property variant host
    property bool loaded: false
    property bool loading: false
    property int serial: 0

    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Fill

    function navRows() {
        var n = 0, bytes = 0, busy = 0, k, on = lib.host && lib.host.signedIn;
        for (k in Yt.dl) { n++; bytes += Yt.dl[k].bytes || 0; if (Yt.dl[k].state === "running" || Yt.dl[k].state === "queued") busy++; }
        return [
            { type: "nav", icon: "download", title: "Downloads", action: "downloads",
              sub: n ? n + (n === 1 ? " video · " : " videos · ") + Yt.fmtBytes(bytes) + (busy ? " · downloading" : "") : "Videos kept on the phone" },
            { type: "nav", icon: "history", title: "History", sub: "", action: "browse", browseId: "FEhistory", params: "" },
            { type: "nav", icon: "later", title: "Watch later", sub: "", action: "browse", browseId: "FEmy_youtube", params: "cAc%3D" },
            { type: "nav", icon: "playlist", title: "Playlists", sub: "", action: "browse", browseId: "FEplaylist_aggregation", params: "" },
            { type: "nav", icon: "videos", title: "Your videos", sub: "", action: "browse", browseId: "FEmy_videos", params: "" },
            { type: "nav", icon: "quality", title: "Settings", sub: "Quality, background play, downloads", action: "settings" },
            { type: "nav", icon: "account", title: on ? "Sign out" : "Sign in", sub: on ? "Signed in" : "Not signed in", action: "account" }
        ];
    }
    // the shortcuts change (download count, account): rebuild the row
    function refreshNav() {
        var rows = navRows(), i;
        shortcuts.removeAll();
        for (i = 0; i < rows.length; i++) {
            var b = shortDef.createObject();
            b.item = rows[i];
            b.picked.connect(lib.pick);
            shortcuts.add(b);
        }
    }
    function pick(item) { lib.host.navAction(item); }
    function load() {
        var my = ++lib.serial;
        refreshNav();
        list.setRows([]);
        lib.loaded = true;
        if (!lib.host || !lib.host.signedIn) return;
        lib.loading = true;
        Yt.api("browse", { browseId: "FElibrary" }, function (o) {
            if (my !== lib.serial) return;
            lib.loading = false;
            if (!o) return;
            var p = Yt.parseSections(o), i, j, rows = [], seen = {};
            for (i = 0; i < p.sections.length; i++)
                for (j = 0; j < p.sections[i].items.length; j++) {
                    var it = p.sections[i].items[j], key = it.id || it.browseId;
                    if (it.type === "link" || !key || seen[key]) continue;
                    seen[key] = true;
                    var c = {}, k;
                    for (k in it) c[k] = it[k];
                    c.card = true;
                    rows.push(c);
                }
            list.setRows(rows);
            console.log("library: " + p.sections.length + " sections, " + list.count() + " cards");
        });
    }
    function ensureLoaded() { if (!lib.loaded) load(); else refreshNav(); }
    function toTop() { list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.Default); }

    ScrollView {                          // the shortcuts
        horizontalAlignment: HorizontalAlignment.Fill
        scrollViewProperties { scrollMode: ScrollMode.Horizontal }
        Container {
            id: shortcuts
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            leftPadding: ui.du(1); rightPadding: ui.du(1); topPadding: ui.du(1.5); bottomPadding: ui.du(1)
        }
    }
    Label {
        visible: list.count() > 0
        text: "Recently watched"
        leftMargin: ui.du(2); topMargin: ui.du(1)
        textStyle { base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.W500; color: Color.White }
    }
    Label {
        visible: list.count() === 0 && !lib.loading
        text: lib.host && lib.host.signedIn ? "Videos you watch show up here." : "Sign in to see your history, playlists and Watch later."
        multiline: true
        leftMargin: ui.du(2); rightMargin: ui.du(2); topMargin: ui.du(3)
        textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#ffaaaaaa") }
    }
    VideoList {
        id: list
        grid: true
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        onPicked: lib.host.openItem(item)
        onAction: lib.host.itemAction(what, item)
    }
    attachedObjects: [
        ComponentDefinition {
            id: shortDef
            Container {                   // a shortcut: icon over a label (the line under it when it says something)
                id: sc
                property variant item
                property bool down: false
                signal picked(variant item)
                preferredWidth: ui.du(22); minWidth: ui.du(22)
                rightMargin: ui.du(1)
                topPadding: ui.du(1.5); bottomPadding: ui.du(1.5)
                background: Color.create(sc.down ? "#ff3a3a3a" : "#ff222222")
                onTouch: { if (event.isDown()) sc.down = true; else if (event.isUp() || event.isCancel()) sc.down = false; }
                gestureHandlers: [ TapHandler { onTapped: sc.picked(sc.item) } ]
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center
                    imageSource: sc.item ? "asset:///icons/" + sc.item.icon + ".png" : ""
                    preferredWidth: ui.du(6); preferredHeight: ui.du(6)
                    scalingMethod: ScalingMethod.AspectFit
                }
                Label {
                    horizontalAlignment: HorizontalAlignment.Center
                    text: sc.item ? sc.item.title : ""
                    textStyle { base: SystemDefaults.TextStyles.SubtitleText; fontWeight: FontWeight.W500; color: Color.White; textAlign: TextAlign.Center }
                }
                Label {
                    horizontalAlignment: HorizontalAlignment.Center
                    visible: sc.item && sc.item.action === "downloads" && sc.item.sub.indexOf("video") > 0
                    topMargin: 0
                    text: sc.item ? sc.item.sub.split(" · ")[0] : ""
                    textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.create("#ffaaaaaa"); textAlign: TextAlign.Center }
                }
            }
        }
    ]
}
