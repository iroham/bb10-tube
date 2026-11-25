// solve.js: run after yt.solver.lib.min.js, glue.js and yt.solver.core.js, with globalThis.__in set by a first tiny
// script. Two modes:
//   { mode: "prep", player: "<path of the downloaded player script>" }
//       -> prints the solver's preprocessed player (text). Slow (the whole player is parsed); done once per player
//          version and cached by the core.
//   { pre: "<path of the cached preprocessed player>", n: ["..."], sig: ["..."] }
//       -> prints JSON { n: {challenge: solved}, sig: {challenge: solved}, error: "..." }.
//   (player: "<path>" instead of pre: solves straight from the full player, slow.)
(function () {
    var input = globalThis.__in, requests = [];
    if (input.mode === "prep") {
        var r = jsc({ type: "player", player: readFile(input.player), requests: [], output_preprocessed: true });
        return r.preprocessed_player;
    }
    if (input.n && input.n.length) requests.push({ type: "n", challenges: input.n });
    if (input.sig && input.sig.length) requests.push({ type: "sig", challenges: input.sig });
    var out = { n: {}, sig: {} };
    try {
        var res = input.pre ? jsc({ type: "preprocessed", preprocessed_player: readFile(input.pre), requests: requests })
                            : jsc({ type: "player", player: readFile(input.player), requests: requests, output_preprocessed: false });
        res.responses.forEach(function (r, i) {
            if (r.type === "result") out[requests[i].type] = r.data;
            else out.error = (out.error ? out.error + "; " : "") + requests[i].type + ": " + String(r.error).split("\n")[0];
        });
    } catch (e) { out.error = String(e); }
    return JSON.stringify(out);
})()
