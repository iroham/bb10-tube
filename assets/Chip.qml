// Chip: a small text button ("720p", "1x").
import bb.cascades 1.3

Container {
    id: chip
    property string text: ""
    property bool down: false
    signal clicked()
    layout: DockLayout {}
    minHeight: ui.du(10)
    leftPadding: ui.du(1); rightPadding: ui.du(1)
    onTouch: { if (event.isDown()) chip.down = true; else if (event.isUp() || event.isCancel()) chip.down = false; }
    gestureHandlers: [ TapHandler { onTapped: chip.clicked() } ]
    Container {
        verticalAlignment: VerticalAlignment.Center
        background: chip.down ? Color.create("#70ffffff") : Color.create("#40ffffff")
        leftPadding: ui.du(1.6); rightPadding: ui.du(1.6); topPadding: ui.du(0.6); bottomPadding: ui.du(0.6)
        Label {
            text: chip.text
            textStyle { base: SystemDefaults.TextStyles.SubtitleText; fontWeight: FontWeight.W500; color: Color.White }
        }
    }
}
