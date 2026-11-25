// SettingsPage: the app's choices, shown in a sheet. Stored by the core in the app's data folder.
import bb.cascades 1.3
import "yt.js" as Yt

Page {
    id: page
    property variant host
    property bool ready: false
    property string storage: ""
    signal done()

    function pick(dd, v) { var i; for (i = 0; i < dd.count(); i++) if (dd.at(i).value === v) dd.selectedIndex = i; }
    // show the stored values (called each time the sheet opens)
    function refresh() {
        page.ready = false;
        pick(quality, Yt.settings.quality); pick(dlQuality, Yt.settings.dlQuality);
        bgToggle.checked = Yt.settings.background; autoToggle.checked = Yt.settings.autoplay; capToggle.checked = Yt.settings.captions;
        var n = 0, bytes = 0, k;
        for (k in Yt.dl) { n++; bytes += Yt.dl[k].bytes || 0; }
        page.storage = n ? n + (n === 1 ? " video, " : " videos, ") + Yt.fmtBytes(bytes) + " on the phone" : "Nothing downloaded";
        page.ready = true;
    }
    function store(key, value) {
        if (!page.ready || Yt.settings[key] === value) return;
        Yt.settings[key] = value;
        page.host.saveSettings();
        console.log("settings: " + key + " = " + value);
    }

    titleBar: TitleBar {
        title: "Settings"
        dismissAction: ActionItem { title: "Done"; onTriggered: page.done() }
    }
    ScrollView {
        Container {
            leftPadding: ui.du(3); rightPadding: ui.du(3); topPadding: ui.du(2); bottomPadding: ui.du(4)
            Header { title: "Playback" }
            DropDown {
                id: quality
                title: "Video quality"
                topMargin: ui.du(2)
                Option { text: "1080p"; value: 1080 }
                Option { text: "720p"; value: 720 }
                Option { text: "480p"; value: 480 }
                Option { text: "360p (least data and battery)"; value: 360 }
                onSelectedValueChanged: page.store("quality", selectedValue)
            }
            Container {
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                topMargin: ui.du(3)
                Label {
                    text: "Keep playing when the app is minimised"
                    multiline: true
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                }
                ToggleButton { id: bgToggle; verticalAlignment: VerticalAlignment.Center; onCheckedChanged: page.store("background", checked) }
            }
            Container {
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                topMargin: ui.du(3)
                Label {
                    text: "Play the next video automatically"
                    multiline: true
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                }
                ToggleButton { id: autoToggle; verticalAlignment: VerticalAlignment.Center; onCheckedChanged: page.store("autoplay", checked) }
            }
            Container {
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                topMargin: ui.du(3); bottomMargin: ui.du(3)
                Label {
                    text: "Show captions when a video has them"
                    multiline: true
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                }
                ToggleButton { id: capToggle; verticalAlignment: VerticalAlignment.Center; onCheckedChanged: page.store("captions", checked) }
            }
            Header { title: "Downloads" }
            DropDown {
                id: dlQuality
                title: "Download quality"
                topMargin: ui.du(2)
                Option { text: "1080p (about 1 GB per hour)"; value: 1080 }
                Option { text: "720p (about 500 MB per hour)"; value: 720 }
                Option { text: "480p"; value: 480 }
                Option { text: "360p (smallest)"; value: 360 }
                onSelectedValueChanged: page.store("dlQuality", selectedValue)
            }
            Label {
                text: page.storage
                bottomMargin: ui.du(3)
                textStyle { base: SystemDefaults.TextStyles.SubtitleText; color: Color.create("#ffa0a0a0") }
            }
            Header { title: "Account" }
            Container {
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                topMargin: ui.du(2); bottomMargin: ui.du(3)
                Label {
                    text: page.host && page.host.signedIn ? "Signed in" : "Not signed in"
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                }
                Button {
                    text: page.host && page.host.signedIn ? "Sign out" : "Sign in"
                    preferredWidth: ui.du(30)
                    onClicked: { page.done(); if (page.host.signedIn) page.host.signOut(); else page.host.signIn(); }
                }
            }
            Header { title: "Touch and keys" }
            Label {
                multiline: true
                topMargin: ui.du(2)
                text: "On the picture: tap for the controls. Double tap left or right to skip 10 seconds, in the middle for full view. Press and hold to wind forward (on the left: back). Swipe down to make the player small, up for full view.\n\nSwipe right anywhere else to go back. Press and hold a video in a list to download or save it.\n\nKeys: Space or K play/pause · J and L 10 s back/forward · F full view · M small player · N next · Q quality · C captions · I like · U watch later · D download · S search · H home · Y you · B back · R refresh."
                textStyle { base: SystemDefaults.TextStyles.SubtitleText; color: Color.create("#ffb0b0b0") }
            }
        }
    }
}
