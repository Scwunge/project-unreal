/* mc-unreal progress tracker widget. Plain JS, no build step, no network access except the progress.json you point it at.
 *
 * Embed:
 *   <div data-mc-tracker data-src="https://example.com/progress.json"></div>
 *   <script src="https://example.com/tracker/tracker.js" defer></script>
 *
 * Options (attributes on the div):
 *   data-src       URL of progress.json (default: progress.json next to the page)
 *   data-interval  seconds between re-fetches (default 300; 0 = fetch once)
 *   data-theme     auto | light | dark (default auto: follows the visitor's system setting)
 *   data-title     heading text (default "mc-unreal port progress")
 *   data-css       URL of the stylesheet (default: tracker.css next to tracker.js; "none" = only your own CSS)
 *   data-missing   "off" hides the missing-id lists
 *
 * Styling: the widget lives in a shadow root, so your site's CSS does not reach inside it; its colours and
 * sizes are CSS custom properties (--mct-*) that you can set on the div. See tracker.css and TRACKER.md.
 */
(function () {
  "use strict";

  var SCRIPT_SRC = (document.currentScript && document.currentScript.src) || "";
  var DEFAULT_CSS = SCRIPT_SRC ? new URL("tracker.css", SCRIPT_SRC).href : "tracker.css";

  function el(tag, cls, text) {
    var node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text !== undefined && text !== null) node.textContent = text;
    return node;
  }

  function svgEl(tag, attrs) {
    var node = document.createElementNS("http://www.w3.org/2000/svg", tag);
    for (var k in attrs) node.setAttribute(k, attrs[k]);
    return node;
  }

  function fmt(n) {
    return Number(n || 0).toLocaleString();
  }

  function pct(part, whole) {
    if (!whole) return "0%";
    var p = (100 * part) / whole;
    return (p > 0 && p < 0.1 ? "<0.1" : p.toFixed(p < 10 ? 1 : 0)) + "%";
  }

  function ago(iso) {
    var t = Date.parse(iso);
    if (isNaN(t)) return "unknown";
    var s = Math.max(0, Math.round((Date.now() - t) / 1000));
    if (s < 60) return "just now";
    var m = Math.round(s / 60);
    if (m < 60) return m + (m === 1 ? " minute ago" : " minutes ago");
    var h = Math.round(m / 60);
    if (h < 48) return h + (h === 1 ? " hour ago" : " hours ago");
    var d = Math.round(h / 24);
    return d + " days ago";
  }

  // A stacked bar: verified | present | absent, widths proportional to the counts.
  function stackBar(c, big) {
    var bar = el("div", "bar" + (big ? " bar-big" : ""));
    bar.setAttribute("role", "img");
    bar.setAttribute("aria-label",
      fmt(c.verified) + " verified, " + fmt(c.present) + " present, " + fmt(c.absent) + " absent of " + fmt(c.ids));
    [["verified", c.verified], ["present", c.present], ["absent", c.absent]].forEach(function (pair) {
      if (!pair[1]) return;
      var seg = el("span", "seg seg-" + pair[0]);
      seg.style.width = (100 * pair[1]) / (c.ids || 1) + "%";
      seg.title = pair[0] + ": " + fmt(pair[1]);
      bar.appendChild(seg);
    });
    return bar;
  }

  function legend(c) {
    var row = el("div", "legend");
    [["verified", "Verified"], ["present", "Present"], ["absent", "Absent"]].forEach(function (pair) {
      var item = el("span", "legend-item");
      item.appendChild(el("span", "dot dot-" + pair[0]));
      item.appendChild(el("span", "legend-label", pair[1]));
      item.appendChild(el("strong", "legend-count", fmt(c[pair[0]])));
      item.appendChild(el("span", "legend-pct", pct(c[pair[0]], c.ids)));
      row.appendChild(item);
    });
    return row;
  }

  // History sparkline: share of ids present-or-verified, and share verified, over the runs.
  function sparkline(history) {
    var wrap = el("div", "spark");
    var points = (history || []).filter(function (h) { return h && h.ids; });
    if (points.length < 2) {
      wrap.appendChild(el("span", "muted", points.length ? "Trend appears after the next update." : ""));
      return wrap;
    }
    var W = 240, H = 44, P = 3;
    var svg = svgEl("svg", { viewBox: "0 0 " + W + " " + H, preserveAspectRatio: "none", class: "spark-svg",
      role: "img", "aria-label": "History of " + points.length + " updates" });
    var times = points.map(function (h) { return Date.parse(h.t) || 0; });
    var t0 = times[0], t1 = times[times.length - 1];
    function x(i) { return t1 > t0 ? P + ((W - 2 * P) * (times[i] - t0)) / (t1 - t0) : P + ((W - 2 * P) * i) / (points.length - 1); }
    // Scale to the range the values actually cover (at least 4 points of percent), so small gains are visible.
    function share(h, key) { return key === "done" ? (h.present + h.verified) / h.ids : h.verified / h.ids; }
    var all = [];
    points.forEach(function (h) { all.push(share(h, "done"), share(h, "verified")); });
    var lo = Math.min.apply(null, all), hi = Math.max.apply(null, all);
    if (hi - lo < 0.04) { var mid = (hi + lo) / 2; lo = Math.max(0, mid - 0.02); hi = Math.min(1, lo + 0.04); }
    function y(v) { return H - P - ((H - 2 * P) * (v - lo)) / (hi - lo); }
    function line(key, cls) {
      var d = points.map(function (h, i) {
        var v = share(h, key);
        return (i ? "L" : "M") + x(i).toFixed(1) + " " + y(v).toFixed(1);
      }).join(" ");
      svg.appendChild(svgEl("path", { d: d, class: cls, "vector-effect": "non-scaling-stroke" }));
    }
    line("done", "spark-done");
    line("verified", "spark-verified");
    wrap.appendChild(svg);
    var first = points[0], last = points[points.length - 1];
    var delta = (last.present + last.verified) - (first.present + first.verified);
    wrap.appendChild(el("div", "spark-caption",
      points.length + " updates since " + new Date(first.t).toLocaleDateString() + " · " +
      (delta >= 0 ? "+" : "") + fmt(delta) + " ids present or verified"));
    return wrap;
  }

  function categoryRow(c, showMissing) {
    var row = el(showMissing && c.absent ? "details" : "div", "cat");
    var head = el(showMissing && c.absent ? "summary" : "div", "cat-head");
    head.appendChild(el("span", "cat-label", c.label));
    head.appendChild(stackBar(c, false));
    var counts = el("span", "cat-counts");
    counts.appendChild(el("strong", null, fmt(c.present + c.verified)));
    counts.appendChild(document.createTextNode(" / " + fmt(c.ids)));
    if (c.verified) counts.appendChild(el("span", "cat-verified", fmt(c.verified) + " verified"));
    head.appendChild(counts);
    head.title = c.rule || "";
    row.appendChild(head);
    if (showMissing && c.absent) {
      var body = el("div", "missing");
      body.appendChild(el("div", "missing-title", "Absent (" + fmt(c.absent) + ")"));
      var list = el("div", "missing-ids");
      (c.missing || []).forEach(function (id) { list.appendChild(el("code", null, id)); });
      if (c.missing_more) list.appendChild(el("span", "muted", "+" + fmt(c.missing_more) + " more"));
      body.appendChild(list);
      if (c.rule) body.appendChild(el("div", "rule", "Present means: " + c.rule + "."));
      row.appendChild(body);
    }
    return row;
  }

  function render(root, data, opts) {
    var t = data.totals || { ids: 0, absent: 0, present: 0, verified: 0 };
    var box = el("section", "card");
    var header = el("header", "head");
    header.appendChild(el("h2", "title", opts.title));
    var meta = el("div", "meta");
    var updated = el("time", "updated", "Updated " + ago(data.generated));
    updated.setAttribute("datetime", data.generated || "");
    updated.title = data.generated ? new Date(data.generated).toLocaleString() : "";
    meta.appendChild(updated);
    if (data.commit && data.commit.hash) {
      meta.appendChild(el("span", "commit", (data.commit.branch ? data.commit.branch + " @ " : "") + data.commit.hash.slice(0, 7)));
    }
    header.appendChild(meta);
    box.appendChild(header);

    var overall = el("div", "overall");
    var big = el("div", "big");
    big.appendChild(el("span", "big-num", fmt(t.present + t.verified)));
    big.appendChild(el("span", "big-of", " of " + fmt(t.ids) + " vanilla ids present or verified"));
    overall.appendChild(big);
    overall.appendChild(stackBar(t, true));
    overall.appendChild(legend(t));
    if (data.category_mean_pct) {
      overall.appendChild(el("div", "mean",
        "Average across " + (data.categories || []).length + " categories: " +
        data.category_mean_pct.present_or_verified + "% present or verified, " +
        data.category_mean_pct.verified + "% verified."));
    }
    if (data.test_log && !data.test_log.used) {
      overall.appendChild(el("div", "note", "No test run was supplied for this commit, so nothing counts as verified yet."));
    }
    box.appendChild(overall);
    box.appendChild(sparkline(data.history));

    var cats = el("div", "cats");
    var groups = [["registry", "Registries"], ["commands", "Commands"], ["data", "Data"]];
    groups.forEach(function (g) {
      var members = (data.categories || []).filter(function (c) { return c.group === g[0]; });
      if (!members.length) return;
      cats.appendChild(el("h3", "group", g[1]));
      members.forEach(function (c) { cats.appendChild(categoryRow(c, opts.missing)); });
    });
    box.appendChild(cats);

    var foot = el("footer", "foot");
    foot.appendChild(el("span", null,
      "Present: the port names or loads the id. Verified: a passing test or a vanilla comparison names it. Counts, not a release date."));
    if (data.skipped && data.skipped.length) {
      foot.appendChild(el("span", "muted", " Not counted: " + data.skipped.map(function (s) { return s.key; }).join(", ") + "."));
    }
    box.appendChild(foot);

    // Keep which category rows are open across refreshes.
    var open = {};
    root.querySelectorAll("details[open] .cat-label").forEach(function (n) { open[n.textContent] = true; });
    box.querySelectorAll("details").forEach(function (d) {
      if (open[d.querySelector(".cat-label").textContent]) d.open = true;
    });

    var old = root.querySelector(".card");
    if (old) root.replaceChild(box, old); else root.appendChild(box);
  }

  function mount(host) {
    if (host.__mcTracker) return;
    host.__mcTracker = true;
    var opts = {
      src: host.getAttribute("data-src") || "progress.json",
      interval: parseFloat(host.getAttribute("data-interval") || "300"),
      theme: host.getAttribute("data-theme") || "auto",
      title: host.getAttribute("data-title") || "mc-unreal port progress",
      css: host.getAttribute("data-css") || DEFAULT_CSS,
      missing: host.getAttribute("data-missing") !== "off"
    };
    if (!host.hasAttribute("data-theme")) host.setAttribute("data-theme", opts.theme);
    var root = host.attachShadow ? host.attachShadow({ mode: "open" }) : host;
    if (opts.css !== "none") {
      var link = document.createElement("link");
      link.rel = "stylesheet";
      link.href = opts.css;
      root.appendChild(link);
    }
    var status = el("div", "status", "Loading progress…");
    status.setAttribute("aria-live", "polite");
    root.appendChild(status);

    var last = null, timer = null;
    function load() {
      // A cache-busting query, so CDNs that ignore no-store still hand out the newest file (not for blob:/data: URLs).
      var url = /^(blob|data):/.test(opts.src) ? opts.src
        : opts.src + (opts.src.indexOf("?") < 0 ? "?" : "&") + "_=" + Date.now();
      fetch(url, { cache: "no-store" })
        .then(function (r) {
          if (!r.ok) throw new Error("HTTP " + r.status);
          return r.json();
        })
        .then(function (data) {
          last = data;
          status.textContent = "";
          status.className = "status";
          render(root, data, opts);
          host.dispatchEvent(new CustomEvent("mc-tracker-update", { detail: data, bubbles: true }));
        })
        .catch(function (err) {
          status.className = "status status-error";
          status.textContent = (last ? "Could not refresh (" : "Could not load progress (") + err.message + ").";
        });
    }
    function schedule() {
      clearInterval(timer);
      if (opts.interval > 0) timer = setInterval(function () { if (!document.hidden) load(); }, opts.interval * 1000);
    }
    document.addEventListener("visibilitychange", function () {
      if (!document.hidden && opts.interval > 0) load();
    });
    // Re-render the "updated N minutes ago" text once a minute without fetching.
    setInterval(function () { if (last) render(root, last, opts); }, 60000);
    load();
    schedule();
  }

  function mountAll() {
    document.querySelectorAll("[data-mc-tracker]").forEach(mount);
  }
  window.McTracker = { mount: mount, mountAll: mountAll };
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", mountAll);
  else mountAll();
})();
