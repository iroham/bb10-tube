#!/bin/bash
# Off-phone test of assets/yt.js with QuickJS (build/host/jsrun) against captured responses in .secrets/api/.
# Prints counts and shapes only, no account content.  tools/test-yt.sh
cd "$(dirname "$0")/.."
T=build/host/yt-test; mkdir -p $T
tail -n +2 assets/yt.js > $T/yt.js      # without the QML ".pragma library" line
cat > $T/run.js <<'JS'
(function () {
    var files = ["channel", "playlist", "cont", "browse-FEtopics", "browse-FElibrary", "browse-FEhistory", "browse-FEmy_youtube", "browse-FEsubscriptions", "search", "next"], out = [];
    files.forEach(function (f) {
        var d = JSON.parse(readFile(".secrets/api/" + f + ".json")), p = parseSections(d), rows = toRows(p);
        var types = {}, bad = 0;
        rows.forEach(function (r) { types[r.type] = (types[r.type] || 0) + 1; if (r.type !== "header" && r.type !== "more" && (!r.title || (!r.thumb && r.type !== "link") || (r.type === "video" && !r.id) || (r.type !== "video" && !r.browseId))) bad++; });
        out.push(f + ": sections " + p.sections.length + " [" + p.sections.map(function (s) { return s.items.length + (s.cont ? "+" : "") + (s.title ? "" : "(untitled)"); }).join(",") + "] page cont " + (p.cont ? "yes" : "no") + " rows " + JSON.stringify(types) + " incomplete " + bad);
        if (f === "next") { var w = parseWatch(d); out.push("watch: title " + w.title.length + " views " + (w.views ? "y" : "n") + " date " + (w.date ? "y" : "n") + " channel " + (w.channel ? "y" : "n") + " id " + w.channelId.substr(0, 2) + " subs " + (w.subs ? "y" : "n") + " like " + w.like + " likes " + (w.likes ? "y" : "n") + " desc " + w.description.length + " next " + (w.next ? "y" : "n") + " related sections " + w.sections.length); }
    });
    out.push("time " + fmtTime(3725000) + " " + fmtTime(59000) + "; caption " + JSON.stringify(captionAt(parseCaptions('{"events":[{"tStartMs":0,"dDurationMs":2000,"segs":[{"utf8":"a"},{"utf8":" b"}]},{"tStartMs":1500,"dDurationMs":2000,"segs":[{"utf8":"c"}]}]}'), 1700)));
    return out.join("\n");
})()
JS
build/host/jsrun $T/yt.js $T/run.js
