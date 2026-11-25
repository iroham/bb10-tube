// IconButton: a flat picture button with an optional small label under it and a pressed highlight.
import bb.cascades 1.3

Container {
    id: btn
    property alias icon: img.imageSource
    property string label: ""
    property real iconSize: ui.du(6)
    property bool dim: false              // drawn weaker (an inactive tab, a switched-off option)
    property bool down: false
    signal clicked()

    layout: DockLayout {}
    minWidth: ui.du(10); minHeight: ui.du(10)
    background: btn.down ? Color.create("#38ffffff") : Color.Transparent
    onTouch: { if (event.isDown()) btn.down = true; else if (event.isUp() || event.isCancel()) btn.down = false; }
    gestureHandlers: [ TapHandler { onTapped: btn.clicked() } ]
    Container {
        horizontalAlignment: HorizontalAlignment.Center
        verticalAlignment: VerticalAlignment.Center
        leftPadding: ui.du(1); rightPadding: ui.du(1)
        ImageView {
            id: img
            preferredWidth: btn.iconSize; preferredHeight: btn.iconSize
            minWidth: btn.iconSize; minHeight: btn.iconSize
            horizontalAlignment: HorizontalAlignment.Center
            scalingMethod: ScalingMethod.AspectFit
            opacity: btn.dim ? 0.55 : 1.0
        }
        Label {
            text: btn.label
            visible: btn.label !== ""
            topMargin: 0
            horizontalAlignment: HorizontalAlignment.Center
            textStyle { base: SystemDefaults.TextStyles.SmallText; color: btn.dim ? Color.create("#ff9a9a9a") : Color.White; fontSize: FontSize.XSmall }
        }
    }
}
