// VideoRow: a list row with a picture: video, channel, playlist or download.
//   ListItemData: { title, sub, sub2, thumb (web address or local file), dur, pct }
import bb.cascades 1.3

Container {
    id: row
    property string shown: ""
    // (re)load the picture when the row is created or reused for another item
    function refresh() {
        var want = ListItemData.thumb;
        if (want === row.shown) return;
        row.shown = want;
        img.resetImageSource();
        if (!want) return;
        if (want.charAt(0) === "/") { img.imageSource = "file://" + want; return; }
        row.ListItem.view.thumbFor(want, function (path, url) { if (url === row.shown) img.imageSource = path; });
    }
    onCreationCompleted: refresh()
    background: row.ListItem.active ? Color.create("#33ffffff") : Color.Transparent
    layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
    topPadding: ui.du(1); bottomPadding: ui.du(1); leftPadding: ui.du(2); rightPadding: ui.du(2)
    horizontalAlignment: HorizontalAlignment.Fill
    Container {
        layout: DockLayout {}
        preferredWidth: ui.du(38); minWidth: ui.du(38); maxWidth: ui.du(38)
        preferredHeight: ui.du(21.4); minHeight: ui.du(21.4); maxHeight: ui.du(21.4)
        background: Color.create("#ff1c1c1c")
        ImageView {
            id: img
            property string src: ListItemData.thumb
            onSrcChanged: row.refresh()
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            scalingMethod: ScalingMethod.AspectFit
        }
        Container {
            visible: ListItemData.dur !== "" && ListItemData.dur !== undefined
            horizontalAlignment: HorizontalAlignment.Right
            verticalAlignment: VerticalAlignment.Bottom
            background: Color.create("#cc000000")
            leftPadding: ui.du(0.8); rightPadding: ui.du(0.8)
            Label {
                text: ListItemData.dur
                textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.White }
            }
        }
        Container {
            visible: ListItemData.pct > 0
            horizontalAlignment: HorizontalAlignment.Left
            verticalAlignment: VerticalAlignment.Bottom
            preferredHeight: ui.du(0.6)
            preferredWidth: ui.du(38) * (ListItemData.pct > 0 ? ListItemData.pct : 0) / 100
            background: Color.create("#ffe53935")
        }
    }
    Container {
        leftMargin: ui.du(2)
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        verticalAlignment: VerticalAlignment.Center
        Label {
            text: ListItemData.title
            multiline: true
            autoSize.maxLineCount: 3
            textStyle { base: SystemDefaults.TextStyles.BodyText; fontWeight: FontWeight.W500; color: Color.White }
        }
        Label {
            text: ListItemData.sub
            visible: ListItemData.sub !== "" && ListItemData.sub !== undefined
            topMargin: 0
            textStyle { base: SystemDefaults.TextStyles.SubtitleText; color: Color.create("#ffb0b0b0") }
        }
        Label {
            text: ListItemData.sub2
            visible: ListItemData.sub2 !== "" && ListItemData.sub2 !== undefined
            topMargin: 0
            multiline: true
            autoSize.maxLineCount: 2
            textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.create("#ff909090") }
        }
    }
}
