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
 *   data-title     heading text (default "mc-unreal: how close to 1:1")
 *   data-css       URL of the stylesheet (default: tracker.css next to tracker.js; "none" = only your own CSS)
 *   data-missing   "off" hides the lists of missing ids in the drill-downs
 *   data-systems   "off" hides the systems list
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

  function fmt(n) { return Number(n || 0).toLocaleString(); }
  function pct1(x) { return (Math.round(x * 10) / 10).toFixed(1) + "%"; }
  function share(x) { return pct1(100 * (x || 0)); }

  function ago(iso) {
    var t = Date.parse(iso);
    if (isNaN(t)) return "unknown";
    var s = Math.max(0, Math.round((Date.now() - t) / 1000));
    if (s < 60) return "just now";
    var m = Math.round(s / 60);
    if (m < 60) return m + (m === 1 ? " minute ago" : " minutes ago");
    var h = Math.round(m / 60);
    if (h < 48) return h + (h === 1 ? " hour ago" : " hours ago");
    return Math.round(h / 24) + " days ago";
  }

  // A stacked bar of checks: verified | present | absent.
  function stackBar(c, big) {
    var total = (c.verified || 0) + (c.present || 0) + (c.absent || 0);
    var bar = el("div", "bar" + (big ? " bar-big" : ""));
    bar.setAttribute("role", "img");
    bar.setAttribute("aria-label", fmt(c.verified) + " verified, " + fmt(c.present) + " present, " + fmt(c.absent) + " absent");
    [["verified", c.verified], ["present", c.present], ["absent", c.absent]].forEach(function (p) {
      if (!p[1]) return;
      var seg = el("span", "seg seg-" + p[0]);
      seg.style.width = (100 * p[1]) / (total || 1) + "%";
      seg.title = p[0] + ": " + fmt(p[1]);
      bar.appendChild(seg);
    });
    return bar;
  }

  // A bar for a 0..1 score: the 1:1 share filled, the strict share darker inside it.
  function scoreBar(score, strict, lenient) {
    var bar = el("div", "bar score-bar");
    var len = el("span", "score-lenient"); len.style.width = 100 * (lenient || 0) + "%";
    var sc = el("span", "score-main"); sc.style.width = 100 * (score || 0) + "%";
    var st = el("span", "score-strict"); st.style.width = 100 * (strict || 0) + "%";
    bar.appendChild(len); bar.appendChild(sc); bar.appendChild(st);
    bar.setAttribute("role", "img");
    bar.setAttribute("aria-label", "1:1 " + share(score) + ", strict " + share(strict) + ", lenient " + share(lenient));
    return bar;
  }

  function sparkline(history) {
    var wrap = el("div", "spark");
    var points = (history || []).filter(function (h) { return h && typeof h.one_to_one === "number"; });
    if (points.length < 2) {
      wrap.appendChild(el("span", "muted", points.length ? "The trend line appears after the next update." : ""));
      return wrap;
    }
    var W = 240, H = 48, P = 3;
    var svg = svgEl("svg", { viewBox: "0 0 " + W + " " + H, preserveAspectRatio: "none", class: "spark-svg",
      role: "img", "aria-label": "1:1 percentage over " + points.length + " updates" });
    var times = points.map(function (h) { return Date.parse(h.t) || 0; });
    var t0 = times[0], t1 = times[times.length - 1];
    var keys = ["lenient", "one_to_one", "strict"];
    var all = [];
    points.forEach(function (h) { keys.forEach(function (k) { all.push(h[k]); }); });
    var lo = Math.min.apply(null, all), hi = Math.max.apply(null, all);
    if (hi - lo < 4) { var mid = (hi + lo) / 2; lo = Math.max(0, mid - 2); hi = Math.min(100, lo + 4); }
    function x(i) { return t1 > t0 ? P + ((W - 2 * P) * (times[i] - t0)) / (t1 - t0) : P + ((W - 2 * P) * i) / (points.length - 1); }
    function y(v) { return H - P - ((H - 2 * P) * (v - lo)) / (hi - lo); }
    keys.forEach(function (k) {
      var d = points.map(function (h, i) { return (i ? "L" : "M") + x(i).toFixed(1) + " " + y(h[k]).toFixed(1); }).join(" ");
      svg.appendChild(svgEl("path", { d: d, class: "spark-" + k, "vector-effect": "non-scaling-stroke" }));
    });
    wrap.appendChild(svg);
    var first = points[0], last = points[points.length - 1];
    var delta = Math.round((last.one_to_one - first.one_to_one) * 100) / 100;
    wrap.appendChild(el("div", "spark-caption",
      points.length + " updates since " + new Date(first.t).toLocaleDateString() + " · 1:1 " +
      (delta >= 0 ? "+" : "") + delta.toFixed(2) + " points"));
    return wrap;
  }

  function idList(title, ids, more) {
    var box = el("div", "missing");
    box.appendChild(el("div", "missing-title", title));
    var list = el("div", "missing-ids");
    (ids || []).forEach(function (id) { list.appendChild(el("code", null, id)); });
    if (more) list.appendChild(el("span", "muted", "+" + fmt(more) + " more"));
    box.appendChild(list);
    return box;
  }

  function categoryRow(c, opts) {
    var row = el("details", "cat");
    var head = el("summary", "cat-head");
    var label = el("span", "cat-label", c.label);
    head.appendChild(label);
    head.appendChild(scoreBar(c.score, c.strict, c.lenient));
    var nums = el("span", "cat-counts");
    nums.appendChild(el("strong", null, share(c.score)));
    nums.appendChild(el("span", "cat-sub", fmt(c.ids) + " ids · " + fmt(c.checks.absent) + " checks absent"));
    head.appendChild(nums);
    row.appendChild(head);

    var body = el("div", "cat-body");
    body.appendChild(el("div", "cat-line",
      "Strict " + share(c.strict) + " · lenient " + share(c.lenient) + " · " + fmt(c.ids_done) + " ids done, " +
      fmt(c.ids_untouched) + " untouched · weight " + c.weight));
    var table = el("div", "aspects");
    (c.aspects || []).forEach(function (a) {
      var ar = el("details", "aspect");
      var ah = el("summary", "aspect-head");
      ah.appendChild(el("span", "aspect-label", a.label));
      ah.appendChild(stackBar(a, false));
      ah.appendChild(el("span", "aspect-counts",
        fmt(a.verified) + " ✓ · " + fmt(a.present) + " · " + fmt(a.absent) + " ✗ of " + fmt(a.applies)));
      ar.appendChild(ah);
      var ab = el("div", "aspect-body");
      ab.appendChild(el("div", "rule", a.rule));
      if (opts.missing && a.absent) ab.appendChild(idList("Absent", a.missing, a.missing_more));
      ar.appendChild(ab);
      table.appendChild(ar);
    });
    body.appendChild(table);
    if (opts.missing && c.lowest && c.lowest.length) {
      body.appendChild(idList("Lowest scoring ids", c.lowest, c.lowest_more));
    }
    (c.notes || []).forEach(function (n) { body.appendChild(el("div", "rule", n)); });
    row.appendChild(body);
    return row;
  }

  function systemsBlock(systems) {
    var wrap = el("details", "cat systems");
    var head = el("summary", "cat-head");
    head.appendChild(el("span", "cat-label", "Systems"));
    var counts = { verified: 0, present: 0, absent: 0 };
    systems.forEach(function (s) {
      if (s.status === "verified") counts.verified++;
      else if (s.status === "present") counts.present++;
      else counts.absent++;
    });
    head.appendChild(stackBar(counts, false));
    head.appendChild(el("span", "cat-counts", counts.verified + " verified · " + counts.present + " present · " +
      counts.absent + " absent/unknown"));
    wrap.appendChild(head);
    var list = el("div", "sys-list");
    systems.forEach(function (s) {
      var row = el("div", "sys");
      row.appendChild(el("span", "chip chip-" + (s.status === "unknown" ? "absent" : s.status), s.status));
      var text = el("div", "sys-text");
      text.appendChild(el("div", "sys-label", s.label));
      if (s.evidence) text.appendChild(el("div", "rule", s.evidence));
      (s.notes || []).forEach(function (n) { text.appendChild(el("div", "rule note", n)); });
      row.appendChild(text);
      list.appendChild(row);
    });
    wrap.appendChild(list);
    return wrap;
  }

  function render(root, data, opts) {
    var h = data.headline || {};
    var left = data.left || {};
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

    var hero = el("div", "hero");
    var main = el("div", "hero-main");
    main.appendChild(el("span", "big-num", pct1(h.one_to_one_pct || 0)));
    main.appendChild(el("span", "big-of", "of the way to 1:1"));
    hero.appendChild(main);
    var side = el("div", "hero-side");
    [["Strict", h.strict_pct, "verified only"], ["Lenient", h.lenient_pct, "present or better"]].forEach(function (p) {
      var item = el("div", "hero-item");
      item.appendChild(el("span", "hero-num", pct1(p[1] || 0)));
      item.appendChild(el("span", "hero-label", p[0] + " · " + p[2]));
      side.appendChild(item);
    });
    hero.appendChild(side);
    box.appendChild(hero);
    box.appendChild(scoreBar((h.one_to_one_pct || 0) / 100, (h.strict_pct || 0) / 100, (h.lenient_pct || 0) / 100));

    var leftBox = el("div", "left");
    leftBox.appendChild(el("div", "left-title", "What's left"));
    var grid = el("div", "left-grid");
    [[left.absent, "checks absent"], [left.present_not_verified, "present, not verified"],
     [left.verified, "verified"], [(left.ids || 0) - (left.ids_done || 0), "ids not done of " + fmt(left.ids)],
     [left.systems_absent_or_unknown, "systems missing"], [(left.systems || 0) - (left.systems_verified || 0), "systems not verified"]]
      .forEach(function (p) {
        var cell = el("div", "left-cell");
        cell.appendChild(el("strong", null, fmt(p[0])));
        cell.appendChild(el("span", null, p[1]));
        grid.appendChild(cell);
      });
    leftBox.appendChild(grid);
    box.appendChild(leftBox);
    box.appendChild(sparkline(data.history));

    var ev = data.evidence || {};
    var tl = ev.test_log || {};
    box.appendChild(el("div", "note", tl.used
      ? "Verified from a test run of " + (tl.commit || "?").slice(0, 7) + " (" + fmt(tl.tests_passed) + " passed, " +
        fmt(tl.tests_failed) + " failed), vanilla ref logs and PLAN.md comparisons."
      : "No test run matched this commit, so only vanilla ref logs and PLAN.md comparisons count as verified."));

    var cats = el("div", "cats");
    cats.appendChild(el("h3", "group", "Content"));
    (data.categories || []).filter(function (c) { return c.group !== "data"; })
      .forEach(function (c) { cats.appendChild(categoryRow(c, opts)); });
    cats.appendChild(el("h3", "group", "Data"));
    (data.categories || []).filter(function (c) { return c.group === "data"; })
      .forEach(function (c) { cats.appendChild(categoryRow(c, opts)); });
    if (opts.systems && data.systems && data.systems.length) {
      cats.appendChild(el("h3", "group", "Game systems"));
      cats.appendChild(systemsBlock(data.systems));
    }
    box.appendChild(cats);

    var foot = el("footer", "foot");
    foot.appendChild(el("span", null,
      "1:1 = weighted mean of (verified + ½ present) over every check. A check is one aspect of one vanilla id " +
      "(a block's model, a mob's AI, a command's branch) or one game system. Detected from code and tests; counts, not a release date."));
    box.appendChild(foot);

    // Keep open rows open across refreshes.
    var open = {};
    root.querySelectorAll("details[open]").forEach(function (d) {
      var l = d.querySelector(".cat-label, .aspect-label");
      if (l) open[l.textContent] = true;
    });
    box.querySelectorAll("details").forEach(function (d) {
      var l = d.querySelector(".cat-label, .aspect-label");
      if (l && open[l.textContent]) d.open = true;
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
      title: host.getAttribute("data-title") || "mc-unreal: how close to 1:1",
      css: host.getAttribute("data-css") || DEFAULT_CSS,
      missing: host.getAttribute("data-missing") !== "off",
      systems: host.getAttribute("data-systems") !== "off"
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
          if (!data || !data.headline) throw new Error("not a schema 2 progress.json");
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
    if (opts.interval > 0) timer = setInterval(function () { if (!document.hidden) load(); }, opts.interval * 1000);
    document.addEventListener("visibilitychange", function () {
      if (!document.hidden && opts.interval > 0) load();
    });
    setInterval(function () { if (last) render(root, last, opts); }, 60000);  // keeps "N minutes ago" current
    load();
  }

  function mountAll() { document.querySelectorAll("[data-mc-tracker]").forEach(mount); }
  window.McTracker = { mount: mount, mountAll: mountAll };
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", mountAll);
  else mountAll();
})();
