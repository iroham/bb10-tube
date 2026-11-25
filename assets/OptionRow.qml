// OptionRow: one choice in the player's quality / speed panel.
import bb.cascades 1.3

Container {
    id: opt
    property string text: ""
    property bool checked: false
    property bool down: false
    signal picked()
    horizontalAlignment: HorizontalAlignment.Fill
    minWidth: ui.du(44)
    background: opt.down ? Color.create("#38ffffff") : Color.Transparent
    topPadding: ui.du(1.6); bottomPadding: ui.du(1.6); leftPadding: ui.du(3); rightPadding: ui.du(3)
    onTouch: { if (event.isDown()) opt.down = true; else if (event.isUp() || event.isCancel()) opt.down = false; }
    gestureHandlers: [ TapHandler { onTapped: opt.picked() } ]
    Label {
        text: opt.text + (opt.checked ? "   (now)" : "")
        textStyle { base: SystemDefaults.TextStyles.PrimaryText; fontWeight: opt.checked ? FontWeight.W500 : FontWeight.Normal; color: opt.checked ? Color.create("#ffff6659") : Color.White }
    }
}
