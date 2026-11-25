// nav.js: script state of the main screen: the pages opened on top of each tab, and the swipe being tracked
var stacks = { home: [], subs: [], search: [], lib: [] };
var sx = 0, sy = 0, st = 0, track = false;
var script = [], scriptAt = 0;      // test steps (see main.qml: runScript)
