// state.js: plain script state of the player view (kept out of QML properties)
var info = null;        // parsed "next" response: title, channel, related...
var play = null;        // the core's /play answer
var captions = null;    // parsed caption track
// touch tracking on the picture
var tx = 0, ty = 0, lx = 0, moved = false, held = false, drag = false, holdDir = 1, holdN = 0, holdTo = 0, lastTap = 0, lastZone = 9, skipSum = 0;
