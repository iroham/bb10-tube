// VideoCard: a home feed entry as the phone apps show it: the picture across the whole width (16:9) with the
// length in the corner and a red line for the part already watched, then the title (two lines) and
// "channel · views · age" under it.
import bb.cascades 1.3

Container {
    id: card
    property string shown: ""
    property bool small: card.ListItem.view ? card.ListItem.view.grid : false
    property real w: !card.ListItem.view ? 1440 : small ? (card.ListItem.view.cardW - card.ListItem.view.gap) / 2 : card.ListItem.view.cardW
    function refresh() {
        var want = ListItemData.thumbBig ? ListItemData.thumbBig : ListItemData.thumb;
        if (want === card.shown) return;
        card.shown = want;
        img.resetImageSource();
        if (!want) return;
        card.ListItem.view.thumbFor(want, function (path, url) { if (url === card.shown) img.imageSource = path; });
    }
    onCreationCompleted: refresh()
    horizontalAlignment: HorizontalAlignment.Fill
    bottomPadding: card.small ? 0 : ui.du(3)
    background: card.ListItem.active ? Color.create("#22ffffff") : Color.Transparent
    Container {
        layout: DockLayout {}
        preferredWidth: card.w; preferredHeight: card.w * 9 / 16; minHeight: card.w * 9 / 16; maxHeight: card.w * 9 / 16
        background: Color.create("#ff1c1c1c")
        ImageView {
            id: img
            property string src: ListItemData.thumbBig ? ListItemData.thumbBig : ListItemData.thumb
            onSrcChanged: card.refresh()
            horizontalAlignment: HorizontalAlignment.Fill; verticalAlignment: VerticalAlignment.Fill
            // 4:3 pictures with black bands are cropped to 16:9; a channel's round picture is shown whole
            scalingMethod: ListItemData.type === "channel" ? ScalingMethod.AspectFit : ScalingMethod.AspectFill
        }
        Container {
            visible: ListItemData.dur !== "" && ListItemData.dur !== undefined
            horizontalAlignment: HorizontalAlignment.Right; verticalAlignment: VerticalAlignment.Bottom
            rightMargin: ui.du(1.5); bottomMargin: ui.du(1.5)
            Container {
                background: Color.create("#cc000000")
                leftPadding: ui.du(1); rightPadding: ui.du(1); topPadding: ui.du(0.2); bottomPadding: ui.du(0.2)
                Label { text: ListItemData.dur; textStyle { base: SystemDefaults.TextStyles.SmallText; fontWeight: FontWeight.W500; color: Color.White } }
            }
        }
        Container {
            visible: ListItemData.pct > 0
            horizontalAlignment: HorizontalAlignment.Left; verticalAlignment: VerticalAlignment.Bottom
            preferredHeight: ui.du(0.7)
            preferredWidth: card.w * (ListItemData.pct > 0 ? ListItemData.pct : 0) / 100
            background: Color.create("#ffff0033")
        }
    }
    Container {
        leftPadding: card.small ? ui.du(1) : ui.du(2.5); rightPadding: card.small ? ui.du(1) : ui.du(2.5); topPadding: card.small ? ui.du(1) : ui.du(1.5)
        horizontalAlignment: HorizontalAlignment.Fill
        Label {
            text: ListItemData.title
            multiline: true
            autoSize.maxLineCount: 2
            bottomMargin: 0
            textStyle { base: card.small ? SystemDefaults.TextStyles.BodyText : SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.W500; color: Color.White }
        }
        Label {
            topMargin: card.small ? 0 : ui.du(0.5)
            text: (ListItemData.sub ? ListItemData.sub : "") + (ListItemData.sub && ListItemData.sub2 ? " · " : "") + (ListItemData.sub2 ? ListItemData.sub2 : "")
            multiline: true
            autoSize.maxLineCount: 2
            textStyle { base: card.small ? SystemDefaults.TextStyles.SmallText : SystemDefaults.TextStyles.SubtitleText; color: Color.create("#ffaaaaaa") }
        }
    }
}
