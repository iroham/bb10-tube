// VideoList: the one list used everywhere. Rows are flat objects made by yt.js:
//   header {title} | video/channel/playlist {title, sub, sub2, thumb, dur, pct} | link/more {title}
//   nav {icon, title, sub} | meta {title, line, desc}
// Press and hold a video for its menu (download, watch later).
import bb.cascades 1.3
import "yt.js" as Yt

ListView {
    id: lv
    signal picked(variant item, int index)
    signal endReached()
    signal action(string what, variant item)

    property alias model: rows
    property real cardW: 1440               // the list's width: cards size their pictures from it
    property bool grid: false               // the home feed: cards two to a row
    property real gap: ui.du(1.5)
    onGridChanged: { lv.layout = grid ? gridLayout : stackLayout; console.log("list layout: " + (lv.layout === gridLayout ? "grid" : "stack")); }
    scrollRole: ScrollRole.Main

    function setRows(list) { rows.clear(); if (list.length) rows.append(list); }
    function appendRows(list) { if (list.length) rows.append(list); }
    function count() { return rows.size(); }
    // used by the rows: fetch a picture into the cache, then call back with the local address
    function thumbFor(url, cb) { Yt.thumb(url, cb); }
    function ctxAt(what, indexPath) { var it = rows.data(indexPath); if (it) lv.action(what, it); }

    dataModel: ArrayDataModel { id: rows }

    function itemType(data, indexPath) {
        var t = data.type;
        return t === "header" ? "header" : t === "meta" ? "meta" : (t === "more" || t === "link") ? "link" : t === "nav" ? "nav" :
               data.card ? (t === "video" ? "card" : "cardplain") :
               t === "video" ? (data.dl ? "dl" : "item") : "plain";
    }
    onTriggered: {
        var it = rows.data(indexPath);
        if (it && it.type !== "header") lv.picked(it, indexPath[0]);
    }
    attachedObjects: [
        ListScrollStateHandler { onAtEndChanged: if (atEnd && rows.size() > 0) lv.endReached() },
        LayoutUpdateHandler { onLayoutFrameChanged: if (layoutFrame.width > 0) lv.cardW = layoutFrame.width },
        StackListLayout { id: stackLayout },
        GridListLayout { id: gridLayout; columnCount: 2; cellAspectRatio: 1.12; horizontalCellSpacing: lv.gap; verticalCellSpacing: lv.gap }
    ]

    listItemComponents: [
        ListItemComponent {
            type: "header"
            Container {
                topPadding: ui.du(2.5); bottomPadding: ui.du(1); leftPadding: ui.du(2); rightPadding: ui.du(2)
                Label {
                    text: ListItemData.title
                    textStyle { base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.W500; color: Color.White }
                }
            }
        },
        ListItemComponent {
            type: "link"
            Container {
                id: linkRow
                background: linkRow.ListItem.active ? Color.create("#33ffffff") : Color.Transparent
                topPadding: ui.du(2); bottomPadding: ui.du(2); leftPadding: ui.du(2.5); rightPadding: ui.du(2)
                horizontalAlignment: HorizontalAlignment.Fill
                Label {
                    text: ListItemData.title
                    textStyle { base: SystemDefaults.TextStyles.PrimaryText; color: Color.create("#ff8ab4f8") }
                }
            }
        },
        ListItemComponent {
            type: "nav"
            Container {
                id: navRow
                background: navRow.ListItem.active ? Color.create("#33ffffff") : Color.Transparent
                layout: StackLayout { orientation: LayoutOrientation.LeftToRight }
                topPadding: ui.du(1.8); bottomPadding: ui.du(1.8); leftPadding: ui.du(3); rightPadding: ui.du(2)
                horizontalAlignment: HorizontalAlignment.Fill
                ImageView {
                    imageSource: "asset:///icons/" + ListItemData.icon + ".png"
                    preferredWidth: ui.du(6); preferredHeight: ui.du(6); minWidth: ui.du(6); minHeight: ui.du(6)
                    verticalAlignment: VerticalAlignment.Center
                    scalingMethod: ScalingMethod.AspectFit
                }
                Container {
                    leftMargin: ui.du(3)
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties { spaceQuota: 1 }
                    Label {
                        text: ListItemData.title
                        textStyle { base: SystemDefaults.TextStyles.PrimaryText; color: Color.White }
                    }
                    Label {
                        text: ListItemData.sub
                        visible: ListItemData.sub !== "" && ListItemData.sub !== undefined
                        topMargin: 0
                        textStyle { base: SystemDefaults.TextStyles.SmallText; color: Color.create("#ffa0a0a0") }
                    }
                }
            }
        },
        ListItemComponent {
            type: "meta"
            Container {
                id: metaRow
                background: metaRow.ListItem.active ? Color.create("#22ffffff") : Color.Transparent
                topPadding: ui.du(1.5); bottomPadding: ui.du(1.5); leftPadding: ui.du(2); rightPadding: ui.du(2)
                horizontalAlignment: HorizontalAlignment.Fill
                Label {
                    text: ListItemData.title
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.TitleText; fontWeight: FontWeight.W500; color: Color.White }
                }
                Label {
                    text: ListItemData.line
                    visible: ListItemData.line !== ""
                    multiline: true
                    textStyle { base: SystemDefaults.TextStyles.SubtitleText; color: Color.create("#ffb0b0b0") }
                }
                Label {
                    text: ListItemData.desc
                    visible: ListItemData.desc !== ""
                    multiline: true
                    autoSize.maxLineCount: ListItemData.expanded ? 400 : 3
                    textStyle { base: SystemDefaults.TextStyles.BodyText; color: Color.create("#ffd8d8d8") }
                }
            }
        },
        ListItemComponent {
            type: "plain"
            VideoRow { }
        },
        ListItemComponent {
            type: "item"
            VideoRow {
                id: vRow
                contextActions: [
                    ActionSet {
                        title: ListItemData.title
                        ActionItem {
                            title: "Download"
                            imageSource: "asset:///icons/download.png"
                            onTriggered: vRow.ListItem.view.ctxAt("dl", vRow.ListItem.indexPath)
                        }
                        ActionItem {
                            title: "Save to Watch later"
                            imageSource: "asset:///icons/later.png"
                            onTriggered: vRow.ListItem.view.ctxAt("wl", vRow.ListItem.indexPath)
                        }
                    }
                ]
            }
        },
        ListItemComponent {
            type: "cardplain"
            VideoCard { }
        },
        ListItemComponent {
            type: "card"
            VideoCard {
                id: cRow
                contextActions: [
                    ActionSet {
                        title: ListItemData.title
                        ActionItem {
                            title: "Download"
                            imageSource: "asset:///icons/download.png"
                            onTriggered: cRow.ListItem.view.ctxAt("dl", cRow.ListItem.indexPath)
                        }
                        ActionItem {
                            title: "Save to Watch later"
                            imageSource: "asset:///icons/later.png"
                            onTriggered: cRow.ListItem.view.ctxAt("wl", cRow.ListItem.indexPath)
                        }
                    }
                ]
            }
        },
        ListItemComponent {
            type: "dl"
            VideoRow {
                id: dRow
                contextActions: [
                    ActionSet {
                        title: ListItemData.title
                        DeleteActionItem {
                            title: "Remove download"
                            onTriggered: dRow.ListItem.view.ctxAt("undl", dRow.ListItem.indexPath)
                        }
                    }
                ]
            }
        }
    ]
}
