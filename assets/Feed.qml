// Feed: a list fed by one API call (browse or search), with paging, "More" rows for shelves, a status area and,
// for pages opened from another one, a title strip with a back button.
import bb.cascades 1.3
import "yt.js" as Yt
import "home.js" as Home

Container {
    id: feed
    property variant host
    property string title: ""
    property bool showBack: false
    property string rootBrowse: ""      // a tab's own page: what ensureLoaded() fetches
    property string endpoint: "browse"
    property string bodyJson: "{}"
    property bool loading: false
    property bool loaded: false
    property string cont: ""
    property int nextSid: 0
    property int serial: 0            // answers to an older load are ignored
    property string message: ""
    property bool offerSignIn: false
    property bool isDownloads: false
    property bool home: false          // the home tab: one mixed feed of cards plus topic chips (home.js)
    property bool cards: !isDownloads  // a feed of cards two to a row (all lists but downloads): home.js keeps its state
    property string topic: "All"
    property variant chipList: []

    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Fill

    function load(ep, b) {
        feed.endpoint = ep; feed.bodyJson = JSON.stringify(b);
        reload();
    }
    function ensureLoaded() { if (!feed.loaded && !feed.loading && feed.rootBrowse) load("browse", { browseId: feed.rootBrowse }); }
    function reload() {
        var my = ++feed.serial;
        feed.loading = true; feed.message = ""; feed.offerSignIn = false; feed.cont = ""; feed.nextSid = 0;
        list.setRows([]);
        Yt.apiRaw(feed.endpoint, feed.bodyJson, function (o, st, raw) {
            if (my !== feed.serial) return;
            feed.loading = false; feed.loaded = true;
            if (!o) {
                feed.message = st === 0 || st === 502 ? "No connection." : st === 401 || st === 403 ? "Sign in to see this." : "The service did not answer (" + st + ").";
                feed.offerSignIn = (st === 401 || st === 403) && feed.host && !feed.host.signedIn;
                return;
            }
            var p = Yt.parseSections(o), rows;
            var shape = [];
            for (var si = 0; si < p.sections.length; si++) {
                var sec = p.sections[si], shorts = 0, vids = 0;
                for (var sj = 0; sj < sec.items.length; sj++) { if (sec.items[sj].short) shorts++; else if (sec.items[sj].type === "video") vids++; }
                shape.push(sec.items.length + "(" + vids + "v," + shorts + "s" + (sec.title ? ",t" + sec.title.length : "") + (sec.cont ? ",c" : "") + ")");
            }
            console.log("feed shape: " + shape.join(" ") + (p.cont ? " +pagecont" : ""));
            if (feed.cards) {
                Home.reset(); feed.topic = "All";
                rows = Home.cards(Home.absorb(p, -1, feed.home));
                feed.chipList = Home.chips();
                list.setRows(rows);
                if (!rows.length && !Home.queue.length) {
                    var pr = Yt.findKey(o, "genericPromoRenderer", 10) || Yt.findKey(o, "feedNudgeRenderer", 10);
                    feed.message = pr ? (Yt.text(pr.title) + "\n" + Yt.text(pr.bodyText || pr.subtitle)) : "Nothing here.";
                    feed.offerSignIn = feed.host && !feed.host.signedIn;
                }
                if (rows.length < 12) feed.more();                    // fill the first screens
                return;
            }
            rows = Yt.toRows(p, 0);
            feed.nextSid = p.sections.length; feed.cont = p.cont;
            list.setRows(rows);
            console.log("feed " + feed.endpoint + ": " + p.sections.length + " sections, " + rows.length + " rows, shown " + list.count());
            if (!rows.length) {
                // an empty page usually says why (no subscriptions yet, or "sign in to see recommendations")
                var promo = Yt.findKey(o, "genericPromoRenderer", 10) || Yt.findKey(o, "feedNudgeRenderer", 10);
                feed.message = promo ? (Yt.text(promo.title) + "\n" + Yt.text(promo.bodyText || promo.subtitle)) : "Nothing here.";
                feed.offerSignIn = feed.host && !feed.host.signedIn;
            }
        });
    }
    function more() {                 // next page of the whole feed
        if (feed.cards) { homeMore(); return; }
        if (feed.loading || !feed.cont) return;
        var my = feed.serial, c = feed.cont;
        feed.loading = true; feed.cont = "";
        Yt.api(feed.endpoint, { continuation: c }, function (o) {
            if (my !== feed.serial) return;
            feed.loading = false;
            if (!o) return;
            var p = Yt.parseSections(o), rows = Yt.toRows(p, feed.nextSid);
            feed.nextSid += p.sections.length; feed.cont = p.cont;
            list.appendRows(rows);
        });
    }
    // home: "All" takes the next waiting continuation (a shelf's or the page's); a topic continues its own shelf
    function homeMore() {
        if (feed.loading) return;
        var my = feed.serial, t = -1, c = "", topicName = feed.topic;
        if (topicName === "All") {
            if (!Home.queue.length) return;
            var q = Home.queue.shift(); c = q.cont; t = q.topic;
        } else {
            t = Home.topicIndex(topicName);
            if (t < 0 || !Home.topics[t].cont) return;
            c = Home.topics[t].cont; Home.topics[t].cont = "";
        }
        feed.loading = true;
        Yt.api(feed.endpoint, { continuation: c }, function (o) {
            if (my !== feed.serial) return;
            feed.loading = false;
            if (!o) return;
            var p = Yt.parseSections(o), before = t >= 0 ? Home.topics[t].items.length : 0;
            var fresh = Home.absorb(p, t, feed.home);
            feed.chipList = Home.chips();
            if (feed.topic !== topicName) return;
            if (topicName === "All") {
                list.appendRows(Home.cards(fresh));
                if (fresh.length < 4 && Home.queue.length) feed.homeMore();    // a page of repeats: keep going
            } else list.appendRows(Home.cards(Home.topics[t].items.slice(before)));
        });
    }
    function selectTopic(name) {
        if (name === feed.topic) { toTop(); return; }
        feed.topic = name;
        var t = Home.topicIndex(name);
        list.setRows(Home.cards(name === "All" ? Home.all : (t >= 0 ? Home.topics[t].items : [])));
        list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.None);
        if (name !== "All" && t >= 0 && Home.topics[t].items.length < 8) feed.homeMore();
    }
    onChipListChanged: chipBox.fill()
    function moreOf(item, index) {    // the rest of one shelf: replaces its "More" row
        var my = feed.serial;
        list.model.replace(index, { type: "more", title: "Loading…", cont: "", sid: item.sid });
        Yt.api(feed.endpoint, { continuation: item.cont }, function (o) {
            if (my !== feed.serial) return;
            var cur = list.model.value(index);
            if (!cur || cur.type !== "more" || cur.sid !== item.sid) return;       // the list changed meanwhile
            list.model.removeAt(index);
            if (!o) return;
            var p = Yt.parseSections(o), rows = [], i, j;
            for (i = 0; i < p.sections.length; i++) for (j = 0; j < p.sections[i].items.length; j++) { p.sections[i].items[j].sid = item.sid; rows.push(p.sections[i].items[j]); }
            var c = p.sections.length ? p.sections[p.sections.length - 1].cont : "";
            if (!c) c = p.cont;
            if (c && rows.length) rows.push({ type: "more", title: "More", cont: c, sid: item.sid });
            if (rows.length) list.model.insert(index, rows);
        });
    }
    attachedObjects: [
        ComponentDefinition {
            id: chipDef
            Container {
                id: tc
                property string text: ""
                property bool on: feed.topic === tc.text
                property bool down: false
                signal picked(string name)
                rightMargin: ui.du(1.2)
                background: Color.create(tc.on ? "#fff1f1f1" : (tc.down ? "#ff3f3f3f" : "#ff272727"))
                leftPadding: ui.du(2.2); rightPadding: ui.du(2.2); topPadding: ui.du(1); bottomPadding: ui.du(1)
                onTouch: { if (event.isDown()) tc.down = true; else if (event.isUp() || event.isCancel()) tc.down = false; }
                gestureHandlers: [ TapHandler { onTapped: tc.picked(tc.text) } ]
                Label {
                    text: tc.text
                    textStyle { base: SystemDefaults.TextStyles.SubtitleText; fontWeight: FontWeight.W500; color: tc.on ? Color.create("#ff0f0f0f") : Color.White }
                }
            }
        }
    ]
    function rowCount() { return list.count(); }
    function cardCount() { var n = 0; for (var i = 0; i < list.count(); i++) if (list.model.value(i).card) n++; return n; }
    function toTop() { list.scrollToPosition(ScrollPosition.Beginning, ScrollAnimation.Default); }
    function focusList() { list.requestFocus(); }

    Container {
        visible: feed.title !== ""
        layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
        horizontalAlignment: HorizontalAlignment.Fill
        background: Color.create("#ff1a1a1a")
        IconButton {
            visible: feed.showBack
            icon: "asset:///icons/back.png"
            onClicked: feed.host.back()
        }
        Label {
            text: feed.title
            verticalAlignment: VerticalAlignment.Center
            leftMargin: ui.du(1)
            layoutProperties: StackLayoutProperties { spaceQuota: 1 }
            textStyle { base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.W500; color: Color.White }
        }
        IconButton {
            icon: "asset:///icons/refresh.png"
            iconSize: ui.du(5)
            onClicked: feed.reload()
        }
    }
    ScrollView {                      // home: topic chips (All first), like the phone apps
        visible: feed.home && feed.chipList.length > 1
        horizontalAlignment: HorizontalAlignment.Fill
        scrollViewProperties { scrollMode: ScrollMode.Horizontal }
        Container {
            id: chipBox
            layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
            leftPadding: ui.du(1.5); rightPadding: ui.du(1.5); topPadding: ui.du(1); bottomPadding: ui.du(1)
            function fill() {
                chipBox.removeAll();
                var l = feed.chipList;
                for (var i = 0; i < l.length; i++) {
                    var c = chipDef.createObject();
                    c.text = l[i];
                    c.picked.connect(feed.selectTopic);
                    chipBox.add(c);
                }
            }
        }
    }
    Container {
        layout: DockLayout {}
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        horizontalAlignment: HorizontalAlignment.Fill
        VideoList {
            id: list
            grid: feed.cards
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            onPicked: {
                if (item.type === "more") { if (item.cont) feed.moreOf(item, index); }
                else if (feed.host) feed.host.openItem(item);
            }
            onAction: if (feed.host) feed.host.itemAction(what, item)
            onEndReached: feed.more()
        }
        Container {
            visible: feed.loading && list.count() === 0 || feed.message !== ""
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            leftPadding: ui.du(4); rightPadding: ui.du(4)
            ActivityIndicator {
                running: feed.loading
                visible: feed.loading
                horizontalAlignment: HorizontalAlignment.Center
                preferredWidth: ui.du(12); preferredHeight: ui.du(12)
            }
            Label {
                text: feed.message
                visible: feed.message !== ""
                multiline: true
                horizontalAlignment: HorizontalAlignment.Center
                textStyle { base: SystemDefaults.TextStyles.BodyText; textAlign: TextAlign.Center; color: Color.create("#ffc0c0c0") }
            }
            Button {
                text: feed.offerSignIn ? "Sign in" : "Try again"
                visible: feed.message !== "" && !feed.loading
                horizontalAlignment: HorizontalAlignment.Center
                onClicked: { if (feed.offerSignIn) feed.host.signIn(); else feed.reload(); }
            }
        }
    }
}
