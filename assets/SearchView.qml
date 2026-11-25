// SearchView: query field with suggestions, then results.
import bb.cascades 1.3
import tube 1.0
import "yt.js" as Yt

Container {
    id: view
    property variant host
    property bool showSuggest: false
    property int suggestSerial: 0

    horizontalAlignment: HorizontalAlignment.Fill
    verticalAlignment: VerticalAlignment.Fill

    function ensureLoaded() { if (!feed.loaded) field.requestFocus(); }
    function focusField() { field.requestFocus(); }
    function toTop() { field.text = ""; field.requestFocus(); }
    function run(q) {
        q = q.replace(/^\s+|\s+$/g, "");
        if (!q) return;
        suggestTimer.stop(); view.suggestSerial++;
        view.showSuggest = false;
        field.text = q;
        feed.load("search", { query: q });
        feed.focusList();
    }
    function suggest() {
        var q = field.text, my = ++view.suggestSerial;
        if (!q) { view.showSuggest = false; return; }
        Yt.fetch("https://suggestqueries.google.com/complete/search?client=firefox&ds=yt&q=" + encodeURIComponent(q), false, function (t) {
            if (my !== view.suggestSerial || !t) return;
            var list = [];
            try { list = JSON.parse(t)[1] || []; } catch (e) { list = []; }
            suggestModel.clear();
            if (list.length) suggestModel.append(list.slice(0, 8));
            view.showSuggest = list.length > 0;
        });
    }

    Container {
        topPadding: ui.du(1.5); bottomPadding: ui.du(1); leftPadding: ui.du(2); rightPadding: ui.du(2)
        TextField {
            id: field
            hintText: "Search"
            inputMode: TextFieldInputMode.Text
            input {
                submitKey: SubmitKey.Search
                onSubmitted: view.run(field.text)
            }
            onTextChanging: { if (field.focused) suggestTimer.start(); }
        }
    }
    Container {
        layout: DockLayout {}
        layoutProperties: StackLayoutProperties { spaceQuota: 1 }
        horizontalAlignment: HorizontalAlignment.Fill
        Feed {
            id: feed
            host: view.host
            visible: !view.showSuggest
        }
        ListView {
            id: suggestList
            visible: view.showSuggest
            horizontalAlignment: HorizontalAlignment.Fill
            verticalAlignment: VerticalAlignment.Fill
            dataModel: ArrayDataModel { id: suggestModel }
            listItemComponents: [
                ListItemComponent {
                    Container {
                        id: sRow
                        background: sRow.ListItem.active ? Color.create("#33ffffff") : Color.Transparent
                        topPadding: ui.du(1.8); bottomPadding: ui.du(1.8); leftPadding: ui.du(3); rightPadding: ui.du(2)
                        horizontalAlignment: HorizontalAlignment.Fill
                        Label {
                            text: ListItemData
                            textStyle { base: SystemDefaults.TextStyles.PrimaryText; color: Color.White }
                        }
                    }
                }
            ]
            onTriggered: view.run(suggestModel.data(indexPath))
        }
    }
    attachedObjects: [
        QTimer { id: suggestTimer; interval: 350; singleShot: true; onTimeout: view.suggest() }
    ]
}
