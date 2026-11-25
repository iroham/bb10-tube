.pragma library
// yt.js: shared logic for the QML pages (ES5, runs in the phone's QML engine; also tested off the phone with
// QuickJS: tools/test-yt.sh). Talks only to the app's own core on 127.0.0.1; the core adds the account and the
// client context and speaks to the service.

var core = "";            // "http://127.0.0.1:<port>/<token>", set once by main.qml
var settings = { quality: 720, background: true, autoplay: true, captions: false, dlQuality: 720 };

// ---------------- downloads and watch positions ----------------
var dl = {}, dlList = [];          // what the core has on the phone: by video id, and as a list
function dlRefresh(cb) {
    getJson("/dl/list", function (list) {
        var m = {}, i, busy = false;
        if (list) {
            for (i = 0; i < list.length; i++) { m[list[i].id] = list[i]; if (list[i].state === "running" || list[i].state === "queued") busy = true; }
            dl = m; dlList = list;
        }
        if (cb) cb(busy);
    });
}
function fmtBytes(n) {
    if (n >= 1073741824) return (n / 1073741824).toFixed(1) + " GB";
    if (n >= 1048576) return Math.round(n / 1048576) + " MB";
    return Math.round(n / 1024) + " KB";
}
var resume = {};                   // video id -> [seconds, time saved]; the newest 80 are kept
function resumeGet(id) { return resume[id] ? resume[id][0] : 0; }
function resumeSet(id, sec, durSec) {
    if (!id) return;
    if (sec < 15 || (durSec > 0 && sec > durSec - 20)) { if (!resume[id]) return; delete resume[id]; }
    else resume[id] = [Math.floor(sec), Math.floor(Date.now() / 1000)];
    var keys = [], k;
    for (k in resume) keys.push(k);
    if (keys.length > 80) {
        keys.sort(function (a, b) { return resume[a][1] - resume[b][1]; });
        for (k = 0; k < keys.length - 80; k++) delete resume[keys[k]];
    }
    request("POST", "/store/resume", JSON.stringify(resume), function () {});
}

// ---------------- transport ----------------
function request(method, path, body, cb) {
    var x = new XMLHttpRequest();
    x.onreadystatechange = function () {
        if (x.readyState !== 4) return;
        cb(x.status, x.responseText || "");
    };
    x.open(method, core + path);
    if (body !== null && body !== undefined) x.send(body); else x.send();
}
function getJson(path, cb) {
    request("GET", path, null, function (st, t) {
        var o = null;
        if (st === 200) { try { o = JSON.parse(t); } catch (e) { o = null; } }
        cb(o, st);
    });
}
// POST /youtubei/v1/<endpoint> through the core. cb(response object or null, http status)
function api(endpoint, body, cb) { apiRaw(endpoint, JSON.stringify(body || {}), cb); }
function apiRaw(endpoint, bodyJson, cb) {
    request("POST", "/api/" + endpoint, bodyJson, function (st, t) {
        var o = null;
        if (t) { try { o = JSON.parse(t); } catch (e) { o = null; } }
        cb(st === 200 ? o : null, st, o);
    });
}
// plain GET of a service address through the core; cb(text or null)
function fetch(url, auth, cb) {
    request("GET", "/get?u=" + encodeURIComponent(url) + (auth ? "&auth=1" : ""), null, function (st, t) { cb(st >= 200 && st < 300 ? t : null, st); });
}

// ---------------- thumbnails ----------------
// The core downloads a picture into the app's cache and answers with the file path; ImageView shows local files.
var thumbCache = {}, thumbWaiting = {}, thumbCount = 0;
function thumb(url, cb) {
    if (!url) return;
    if (thumbCache[url]) { cb(thumbCache[url], url); return; }
    if (thumbWaiting[url]) { thumbWaiting[url].push(cb); return; }
    thumbWaiting[url] = [cb];
    request("GET", "/img?u=" + encodeURIComponent(url), null, function (st, t) {
        var list = thumbWaiting[url] || [], i;
        delete thumbWaiting[url];
        if (st !== 200 || !t) return;
        if (thumbCount > 600) { thumbCache = {}; thumbCount = 0; }       // bound the table
        thumbCache[url] = "file://" + t; thumbCount++;
        for (i = 0; i < list.length; i++) list[i](thumbCache[url], url);
    });
}

// ---------------- small helpers ----------------
function text(o) {
    if (o === null || o === undefined) return "";
    if (typeof o === "string") return o;
    if (typeof o.simpleText === "string") return o.simpleText;
    if (typeof o.content === "string") return o.content;
    if (o.runs) { var s = "", i; for (i = 0; i < o.runs.length; i++) s += o.runs[i].text || ""; return s; }
    return "";
}
// first value stored under key anywhere inside o (depth first)
function findKey(o, key, depth) {
    if (!o || typeof o !== "object") return undefined;
    depth = depth === undefined ? 12 : depth;
    if (depth < 0) return undefined;
    var k, r, i;
    if (o instanceof Array) {
        for (i = 0; i < o.length; i++) { r = findKey(o[i], key, depth - 1); if (r !== undefined) return r; }
        return undefined;
    }
    if (o.hasOwnProperty(key)) return o[key];
    for (k in o) if (o.hasOwnProperty(k)) { r = findKey(o[k], key, depth - 1); if (r !== undefined) return r; }
    return undefined;
}
function pickThumb(list) {
    if (!list || !list.length) return "";
    var i, best = list[list.length - 1];
    for (i = 0; i < list.length; i++) if ((list[i].width || 0) >= 300) { best = list[i]; break; }
    var u = best.url || "";
    return u.indexOf("//") === 0 ? "https:" + u : u;
}
// the largest picture (the home feed shows full-width cards)
function pickBig(list) {
    if (!list || !list.length) return "";
    var i, best = list[0];
    for (i = 1; i < list.length; i++) if ((list[i].width || 0) > (best.width || 0)) best = list[i];
    var u = best.url || "";
    return u.indexOf("//") === 0 ? "https:" + u : u;
}
function fmtTime(ms) {
    var s = Math.floor((ms > 0 ? ms : 0) / 1000), h = Math.floor(s / 3600), m = Math.floor(s % 3600 / 60), sec = s % 60;
    return (h ? h + ":" + (m < 10 ? "0" : "") : "") + m + ":" + (sec < 10 ? "0" : "") + sec;
}

// ---------------- list items ----------------
// One flat object per item, as the list model wants it:
//   { type: "video" | "channel" | "playlist", id, browseId, params, title, sub, sub2, thumb, dur, pct, short }
function fromEndpoints(it, cmd) {
    var we = findKey(cmd, "watchEndpoint", 6), re = findKey(cmd, "reelWatchEndpoint", 6), be = findKey(cmd, "browseEndpoint", 6);
    if (we && we.videoId) { it.type = "video"; it.id = we.videoId; if (we.playlistId) it.playlistId = we.playlistId; if (we.startTimeSeconds) it.start = we.startTimeSeconds; }
    else if (re && re.videoId) { it.type = "video"; it.id = re.videoId; it.short = true; }
    else if (be && be.browseId) {
        it.browseId = be.browseId; it.params = be.params || "";
        it.type = be.browseId.indexOf("UC") === 0 ? "channel" : be.browseId.indexOf("FE") === 0 ? "link" : "playlist";
    } else return false;
    return true;
}
function tileItem(t) {
    var it = { type: "", id: "", browseId: "", params: "", title: "", sub: "", sub2: "", thumb: "", dur: "", pct: 0, short: false, start: 0 };
    if (!fromEndpoints(it, t.onSelectCommand)) return null;
    var ct = t.contentType || "";
    if (ct === "TILE_CONTENT_TYPE_PLAYLIST" && it.type === "video" && it.playlistId) { it.type = "playlist"; it.browseId = "VL" + it.playlistId; }
    if (ct === "TILE_CONTENT_TYPE_SHORTS" || ct === "TILE_CONTENT_TYPE_SHORT") it.short = true;
    var h = t.header && t.header.tileHeaderRenderer, m = t.metadata && t.metadata.tileMetadataRenderer, i, j;
    if (h) {
        it.thumb = pickThumb(h.thumbnail && h.thumbnail.thumbnails);
        it.thumbBig = pickBig(h.thumbnail && h.thumbnail.thumbnails);
        var ov = h.thumbnailOverlays || [];
        for (i = 0; i < ov.length; i++) {
            if (ov[i].thumbnailOverlayTimeStatusRenderer) it.dur = text(ov[i].thumbnailOverlayTimeStatusRenderer.text);
            if (ov[i].thumbnailOverlayResumePlaybackRenderer) it.pct = ov[i].thumbnailOverlayResumePlaybackRenderer.percentDurationWatched || 0;
        }
    }
    if (m) {
        it.title = text(m.title);
        var lines = m.lines || [], out = [];
        for (i = 0; i < lines.length; i++) {
            var items = (lines[i].lineRenderer && lines[i].lineRenderer.items) || [], s = "";
            for (j = 0; j < items.length; j++) s += text(items[j].lineItemRenderer && items[j].lineItemRenderer.text);
            if (s) out.push(s);
        }
        it.sub = out[0] || ""; it.sub2 = out.slice(1).join(" · ");
    }
    if (!it.title && h) { var st = findKey(h, "simpleText", 8); if (typeof st === "string") it.title = st; }   // navigation tiles carry their label in the header
    if (it.type === "link" && it.title === "More") return null;
    return it.title || it.thumb ? it : null;
}
function lockupItem(l) {
    var it = { type: "", id: "", browseId: "", params: "", title: "", sub: "", sub2: "", thumb: "", dur: "", pct: 0, short: false, start: 0 };
    var tap = l.rendererContext && l.rendererContext.commandContext && l.rendererContext.commandContext.onTap;
    if (!fromEndpoints(it, tap)) {
        var ct0 = l.contentType || "";
        if (!l.contentId) return null;
        if (ct0 === "LOCKUP_CONTENT_TYPE_CHANNEL") { it.type = "channel"; it.browseId = l.contentId; }
        else if (ct0 === "LOCKUP_CONTENT_TYPE_PLAYLIST" || ct0 === "LOCKUP_CONTENT_TYPE_PODCAST" || ct0 === "LOCKUP_CONTENT_TYPE_ALBUM") { it.type = "playlist"; it.browseId = "VL" + l.contentId; }
        else { it.type = "video"; it.id = l.contentId; }
    }
    if (l.contentType === "LOCKUP_CONTENT_TYPE_SHORT") it.short = true;
    if ((l.contentType === "LOCKUP_CONTENT_TYPE_PLAYLIST" || l.contentType === "LOCKUP_CONTENT_TYPE_ALBUM") && it.type === "video" && l.contentId) { it.type = "playlist"; it.browseId = "VL" + l.contentId; }
    var src = findKey(l.contentImage, "sources", 6);
    it.thumb = pickThumb(src);
    it.thumbBig = pickBig(src);
    var badge = findKey(l.contentImage, "thumbnailBadgeViewModel", 8);
    if (badge) it.dur = text(badge.text);
    var m = l.metadata && l.metadata.lockupMetadataViewModel, i, j;
    if (m) {
        it.title = text(m.title);
        var rows = (m.metadata && m.metadata.contentMetadataViewModel && m.metadata.contentMetadataViewModel.metadataRows) || [], out = [];
        for (i = 0; i < rows.length; i++) {
            var parts = rows[i].metadataParts || [], ps = [];
            for (j = 0; j < parts.length; j++) { var s = text(parts[j].text); if (s) ps.push(s); }
            if (ps.length) out.push(ps.join(" · "));
        }
        it.sub = out[0] || ""; it.sub2 = out.slice(1).join(" · ");
    }
    return it.title || it.thumb ? it : null;
}

// ---------------- pages ----------------
// Any browse/search/next response -> { sections: [{ title, items: [...], cont }], cont }
// Shelves (and library tabs) become sections; items outside a shelf go to an untitled section.
function parseSections(resp) {
    var out = { sections: [], cont: "" }, loose = null;
    function section(title) { var s = { title: title || "", items: [], cont: "" }; out.sections.push(s); return s; }
    function add(sec, it) {
        if (!it) return;
        if (!sec) { if (!loose) loose = section(""); sec = loose; }
        sec.items.push(it);
    }
    function walk(o, sec, depth) {
        if (!o || typeof o !== "object" || depth > 40) return;
        var i, k;
        if (o instanceof Array) { for (i = 0; i < o.length; i++) walk(o[i], sec, depth + 1); return; }
        for (k in o) {
            if (!o.hasOwnProperty(k)) continue;
            var v = o[k];
            if (k === "tileRenderer") add(sec, tileItem(v));
            else if (k === "lockupViewModel") add(sec, lockupItem(v));
            else if (k === "shelfRenderer") {
                var hdr = v.headerRenderer || v.header, title = "";
                if (hdr) { var t = findKey(hdr, "title", 6); title = text(t); }
                if (!title) title = text(v.title);
                walk(v.content, section(title), depth + 1);
            } else if (k === "tabRenderer") {
                if (v.content) walk(v.content, section(typeof v.title === "string" ? v.title : text(v.title)), depth + 1);
            } else if (k === "continuations") {
                var c = v && v[0] && (v[0].nextContinuationData || v[0].reloadContinuationData);
                if (c && c.continuation) { if (sec) { if (!sec.cont) sec.cont = c.continuation; } else if (!out.cont) out.cont = c.continuation; }
            } else if (k === "playerOverlays" || k === "engagementPanels" || k === "transportControls" || k === "frameworkUpdates" || k === "responseContext" ||
                       k === "onSelectCommand" || k === "menu" || k === "autoplay" || k === "adSlotRenderer" || k === "tvMastheadRenderer") {
                // not list content
            } else if (typeof v === "object") walk(v, sec, depth + 1);
        }
    }
    walk(resp, null, 0);
    var keep = [], i;
    for (i = 0; i < out.sections.length; i++) if (out.sections[i].items.length) keep.push(out.sections[i]);
    out.sections = keep;
    return out;
}
// rows for the list model: a header row per titled section, then its items, then a "more" row when there is more
function toRows(parsed, sectionBase) {
    var rows = [], i, j;
    for (i = 0; i < parsed.sections.length; i++) {
        var s = parsed.sections[i], sid = (sectionBase || 0) + i;
        if (s.title) rows.push({ type: "header", title: s.title, sid: sid });
        var n = s.items.length;
        if (n > 12 && s.items[0].short && s.items[n - 1].short) n = 12;      // a shelf of shorts: the first dozen is enough
        for (j = 0; j < n; j++) { s.items[j].sid = sid; rows.push(s.items[j]); }
        if (s.cont) rows.push({ type: "more", title: "More", cont: s.cont, sid: sid });
    }
    return rows;
}

// the watch page: details of the video + related sections, from the "next" response
function parseWatch(resp) {
    var w = { title: "", views: "", date: "", channel: "", channelId: "", channelThumb: "", subs: "", subscribed: false,
              like: "INDIFFERENT", likes: "", description: "", next: "", sections: [] };
    if (!resp) return w;
    var md = findKey(resp, "videoMetadataRenderer", 10);
    if (md) {
        w.title = text(md.title);
        w.views = text(findKey(md.viewCount, "viewCount", 4)) || text(findKey(md.viewCount, "shortViewCount", 4));
        w.date = text(md.publishedTimeText) || text(md.dateText);
        var ow = md.owner && md.owner.videoOwnerRenderer;
        if (ow) {
            w.channel = text(ow.title); w.subs = text(ow.subscriberCountText);
            w.channelThumb = pickThumb(ow.thumbnail && ow.thumbnail.thumbnails);
            var be = findKey(ow.navigationEndpoint, "browseEndpoint", 4);
            if (be) w.channelId = be.browseId;
            var sb = ow.subscribeButton && ow.subscribeButton.subscribeButtonRenderer;
            if (sb) { w.subscribed = !!sb.subscribed; if (!w.channelId) w.channelId = sb.channelId || ""; }
        }
    }
    var lb = findKey(resp.transportControls, "likeButtonRenderer", 8);
    if (lb) { w.like = lb.likeStatus || "INDIFFERENT"; w.likes = text(lb.likeCountText); }
    var ds = findKey(resp.engagementPanels, "expandableVideoDescriptionBodyRenderer", 12);
    if (ds) w.description = text(ds.descriptionBodyText) || text(ds.attributedDescriptionBodyText);
    var ap = findKey(resp.contents, "autoplayVideoRenderer", 10) || findKey(resp.contents, "autoplayEndpointRenderer", 10);
    var ne = ap && findKey(ap, "watchEndpoint", 8);
    if (ne && ne.videoId) w.next = ne.videoId;
    var pv = resp.contents && resp.contents.singleColumnWatchNextResults && resp.contents.singleColumnWatchNextResults.pivot;
    w.sections = parseSections(pv || {}).sections;
    return w;
}

// captions: json3 events -> [{ s: start ms, e: end ms, t: text }]
function parseCaptions(textJson) {
    var out = [], d, i, j;
    try { d = JSON.parse(textJson); } catch (e) { return out; }
    var ev = d.events || [];
    for (i = 0; i < ev.length; i++) {
        var segs = ev[i].segs || [], s = "";
        for (j = 0; j < segs.length; j++) s += segs[j].utf8 || "";
        s = s.replace(/\s+/g, " ").replace(/^ | $/g, "");
        if (!s) continue;
        out.push({ s: ev[i].tStartMs || 0, e: (ev[i].tStartMs || 0) + (ev[i].dDurationMs || 3000), t: s });
    }
    return out;
}
function captionAt(list, ms) {
    var lo = 0, hi = list.length - 1, best = -1;
    while (lo <= hi) { var mid = (lo + hi) >> 1; if (list[mid].s <= ms) { best = mid; lo = mid + 1; } else hi = mid - 1; }
    // auto captions overlap: show the last two that are still on screen
    var s = "";
    if (best > 0 && list[best - 1].e > ms && list[best - 1].t !== list[best].t) s = list[best - 1].t + "\n";
    return best >= 0 && list[best].e > ms ? s + list[best].t : "";
}

function randomId(n) {
    var a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_", s = "", i;
    for (i = 0; i < n; i++) s += a.charAt(Math.floor(Math.random() * 64));
    return s;
}
