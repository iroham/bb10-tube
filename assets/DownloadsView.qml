// DownloadsView: videos kept on the phone. Tap a finished one to play it without a connection; tap a running one
// to pause it, a paused or failed one to continue. Press and hold to remove.
import bb.cascades 1.3
import "yt.js" as Yt

Container {
    id: view
    property variant host
    property bool empty: false
    property bool isDownloads: true

    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Fill

    function rowOf(d) {
        var m = d.meta || {}, pct = d.nseg > 0 ? Math.floor(d.done * 100 / d.nseg) : 0, line;
        if (d.state === "done") line = d.height + "p · " + Yt.fmtBytes(d.bytes);
        else if (d.state === "running") line = "Downloading… " + pct + "% · " + Yt.fmtBytes(d.bytes) + " — tap to pause";
        else if (d.state === "queued") line = "Waiting for its turn";
        else if (d.state === "failed") line = (d.err || "Failed.") + " Tap to try again.";
        else line = "Paused at " + pct + "% — tap to continue";
        return { type: "video", dl: true, id: d.id, browseId: "", params: "", title: m.title || d.id, sub: m.channel || "", sub2: line,
                 thumb: d.thumb || m.thumb || "", dur: m.dur || "", pct: d.state === "done" ? 0 : (pct > 0 ? pct : 1), short: false, start: 0, state: d.state };
    }
    // called by the app whenever the download table was read again: change rows in place so the list keeps its place
    function dlChanged() {
        var list0 = Yt.dlList, rows = [], i, same = list0.length === list.count();
        for (i = 0; i < list0.length; i++) rows.push(rowOf(list0[i]));
        rows.sort(function (a, b) { return a.title < b.title ? -1 : a.title > b.title ? 1 : 0; });
        for (i = 0; same && i < rows.length; i++) { var c = list.model.value(i); if (!c || c.id !== rows[i].id) same = false; }
        if (!same) list.setRows(rows);
        else for (i = 0; i < rows.length; i++) {
            var cur = list.model.value(i);
            if (cur.sub2 !== rows[i].sub2 || cur.pct !== rows[i].pct || cur.thumb !== rows[i].thumb) list.model.replace(i, rows[i]);
        }
        view.empty = rows.length === 0;
    }

    Container {
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        horizontalAlignment: HorizontalAlignment.Fill
        background: Color.create("#ff1a1a1a")
        IconButton {
            icon: "asset:///icons/back.png"
            onClicked: view.host.back()
        }
        Label {
            text: "Downloads"
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(1)
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            textStyle { base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.W500; color: Color.White }
        }
    }
    Container {
        layout: DockLayout {}
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        horizontalAlignment: HorizontalAlignment.Fill
        VideoList {
            id: list
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            onPicked: {
                if (item.state === "done") view.host.openItem(item);
                else if (item.state === "running" || item.state === "queued") view.host.pauseDownload(item.id);
                else view.host.download(item);
            }
            onAction: view.host.itemAction(what, item)
        }
        Label {
            visible: view.empty
            multiline: true
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            text: "No downloads yet.\nPress and hold a video in any list, or use Download under the player."
            textStyle { base: SystemDefaults.TextStyles.BodyText; textAlign: TextAlign.Center; color: Color.create("#ffc0c0c0") }
        }
    }
}
