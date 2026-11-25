// home.js: the state of the home feed (one copy per Feed: no .pragma library). The TV home arrives as topic
// shelves of a few videos each; the app shows them the way the phone apps do: one feed ("All") that mixes the
// topics and keeps loading as you scroll, plus a chip per topic.
//   topics: [{ title, items: [video], cont }]   queue: continuations still to fetch for "All" ({ cont, topic })
var topics = [], seen = {}, queue = [], all = [];

function reset() { topics = []; seen = {}; queue = []; all = []; }

function topicFor(title) {
    for (var i = 0; i < topics.length; i++) if (topics[i].title === title) return i;
    topics.push({ title: title, items: [], ids: {}, cont: "" });
    return topics.length - 1;
}

// a parsed page (yt.js parseSections) into the topics; into topic `into` when it continues one shelf.
// Returns the new videos in "All" order: round-robin across the topics, so the feed mixes them.
// mix false (subscriptions): keep the server's order, shelf after shelf (newest first)
function absorb(parsed, into, mix) {
    var fresh = [], i, j;
    for (i = 0; i < parsed.sections.length; i++) {
        var sec = parsed.sections[i];
        var t = into >= 0 ? into : topicFor(sec.title || "Recommended");
        var list = [], shorts = 0;
        for (j = 0; j < sec.items.length; j++) {
            var it = sec.items[j];
            if (it.short && mix === false && ++shorts > 12) continue;      // a shelf of shorts: the first dozen is enough
            // the home feed takes videos only; other lists (search, channels, playlists) keep channels and playlists
            var key = it.type === "video" ? it.id : it.browseId;
            if (!key || (it.type !== "video" && (mix !== false || (it.type !== "channel" && it.type !== "playlist")))) continue;
            var home = it.short && mix !== false ? topicFor("Shorts") : t;
            if (!topics[home].ids[key]) { topics[home].ids[key] = true; topics[home].items.push(it); }
            if (it.short && mix !== false) continue;
            if (seen[key]) continue;
            seen[key] = true;
            list.push(it);
        }
        if (sec.cont) { topics[t].cont = sec.cont; queue.push({ cont: sec.cont, topic: t }); }
        fresh.push(list);
    }
    if (parsed.cont) queue.push({ cont: parsed.cont, topic: -1 });
    var out = [], k = 0, more = mix !== false;
    if (!more) for (i = 0; i < fresh.length; i++) for (j = 0; j < fresh[i].length; j++) out.push(fresh[i][j]);
    while (more) {
        more = false;
        for (i = 0; i < fresh.length; i++) if (k < fresh[i].length) { out.push(fresh[i][k]); more = true; }
        k++;
    }
    for (i = 0; i < out.length; i++) all.push(out[i]);
    return out;
}

function chips() {
    var out = ["All"];
    for (var i = 0; i < topics.length; i++) if (topics[i].items.length && topics[i].title) out.push(topics[i].title);
    return out;
}
function topicIndex(title) { for (var i = 0; i < topics.length; i++) if (topics[i].title === title) return i; return -1; }

// list rows: full-width cards
function cards(items) {
    var out = [];
    for (var i = 0; i < items.length; i++) {
        var it = items[i], c = {}, k;
        for (k in it) c[k] = it[k];
        c.card = true;
        out.push(c);
    }
    return out;
}
