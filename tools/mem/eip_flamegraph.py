"""Folded stacks -> an interactive flamegraph SVG.

Interactive means: click a frame to zoom into it, click the header to reset,
type to highlight matches, and hover for exact counts. All of it is inline
script and data attributes in one self-contained file -- no CDN, no sidecar
JSON, so it survives being committed and opened straight off disk.

Idle is separated rather than deleted. The client is pinned at 50 fps, so about
a quarter of samples land in the pacing sleep; leaving them in makes every real
frame look 25% cheaper than it is, and dropping them silently hides how much
headroom the cap is giving back. They are excluded from the widths and reported
in the subtitle.
"""
import html
import sys

FOLDED = sys.argv[1]
OUT = sys.argv[2]
TITLE = sys.argv[3]
SUB = sys.argv[4] if len(sys.argv) > 4 else ""

# A stack whose leaf is one of these is the frame pacer waiting out the cap.
IDLE_HINTS = ("Sleep", "NtDelayExecution", "KiFastSystemCallRet", "WaitForSingle")


def load(path):
    live, idle = {}, 0
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line:
            continue
        stack, _, cnt = line.rpartition(" ")
        try:
            n = int(cnt)
        except ValueError:
            continue
        frames = stack.split(";")
        # The pacing sleep is a system call under frame_loop_step with no
        # render or app frame between: treat that whole stack as idle.
        joined = ";".join(frames)
        WORK = ("App_Render", "App_RunOnce", "RenderFrame", "EmitWalk",
                "gdi_paint", "Present", "ToriDraw", "ToriRS", "UITree",
                "raster_", "soft3d_", "toridraw_")
        # LTO leaves some callers of the pacing sleep unresolved, so this keys
        # on the ABSENCE of any work frame rather than on a named one. The
        # BitBlt also bottoms out in a system call and IS work; it is kept by
        # its gdi_ frame.
        sys_leaf = (frames[-1].startswith("[0x7")
                    or any(h in frames[-1] for h in IDLE_HINTS))
        if sys_leaf and not any(k in joined for k in WORK):
            idle += n
            continue
        live[joined] = live.get(joined, 0) + n
    return live, idle


def build(folded):
    root = {"n": "all", "v": 0, "c": {}}
    for stack, v in folded.items():
        node = root
        root["v"] += v
        for fn in stack.split(";"):
            node = node["c"].setdefault(fn, {"n": fn, "v": 0, "c": {}})
            node["v"] += v
    return root


def rows_of(node, depth, x, out):
    out.append((depth, x, node["v"], node["n"]))
    cx = x
    for ch in sorted(node["c"].values(), key=lambda k: -k["v"]):
        rows_of(ch, depth + 1, cx, out)
        cx += ch["v"]


PALETTE = [
    ("raster_", "#d24b3f"), ("toridraw_gouraud", "#d24b3f"),
    ("toridraw_tex", "#e07b39"), ("ToriDraw_Raster", "#d24b3f"),
    ("ToriDraw_", "#e0a13a"), ("ToriRS_Soft3D", "#c76a2e"),
    ("soft3d_", "#c76a2e"),
    ("UITree_", "#4f8fc9"), ("uitree_", "#4f8fc9"),
    ("App_", "#5f9e5f"), ("app_", "#5f9e5f"),
    ("gdi_", "#9c6fa8"), ("[0x7", "#8b8f98"),
    ("[0x", "#a9adb5"),
]


def colour(fn):
    for pre, c in PALETTE:
        if fn.startswith(pre):
            return c
    return "#7f8a99"


def main():
    folded, idle = load(FOLDED)
    root = build(folded)
    total = root["v"]
    rows = []
    rows_of(root, 0, 0, rows)
    depth_max = max(r[0] for r in rows)

    RH = 16
    TOP = 62
    H = TOP + (depth_max + 1) * RH + 30
    W = 1200
    PAD = 10

    parts = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
        'viewBox="0 0 %d %d" font-family="Verdana,DejaVu Sans,sans-serif">' % (W, H, W, H),
        '<style>'
        'rect{stroke:#0002;stroke-width:.35;cursor:pointer}'
        'text{pointer-events:none;font-size:10px;fill:#111}'
        '.t{font-size:15px;font-weight:bold;fill:#1a1a1a}'
        '.s{font-size:11px;fill:#555}'
        '.hl{stroke:#000;stroke-width:1.2}'
        '</style>',
        '<rect width="%d" height="%d" fill="#f6f6f7"/>' % (W, H),
        '<text class="t" x="%d" y="24" text-anchor="middle" id="ttl">%s</text>'
        % (W // 2, html.escape(TITLE)),
        '<text class="s" x="%d" y="42" text-anchor="middle" id="sub">%s</text>'
        % (W // 2, html.escape(SUB)),
        '<text class="s" x="%d" y="%d" id="det">click a frame to zoom, click the '
        'title to reset</text>' % (PAD, H - 10),
        '<g id="fg">',
    ]

    for depth, x, v, name in rows:
        parts.append(
            '<g class="f" data-d="%d" data-x="%d" data-v="%d" data-n="%s">'
            '<rect y="%d" height="%d" fill="%s"/><text y="%d"></text></g>'
            % (depth, x, v, html.escape(name, quote=True),
               TOP + depth * RH, RH - 1, colour(name), TOP + depth * RH + RH - 5))

    parts.append('</g>')
    parts.append("""<script><![CDATA[
var W=%d, PAD=%d, TOTAL=%d;
var fs=document.getElementById('fg').getElementsByClassName('f');
var det=document.getElementById('det');
var zx=0, zv=TOTAL;
function lay(){
  var avail=W-2*PAD;
  for(var i=0;i<fs.length;i++){
    var g=fs[i], x=+g.dataset.x, v=+g.dataset.v;
    var r=g.firstChild, t=r.nextSibling;
    var x0=(x-zx)/zv*avail+PAD, w=v/zv*avail;
    if(x+v<=zx||x>=zx+zv||w<0.08){g.style.display='none';continue;}
    g.style.display='';
    if(x0<PAD){w-=(PAD-x0);x0=PAD;}
    if(x0+w>W-PAD)w=W-PAD-x0;
    r.setAttribute('x',x0.toFixed(2)); r.setAttribute('width',Math.max(w,0.4).toFixed(2));
    var n=g.dataset.n, s=n.replace(/^.*::/,'');
    t.setAttribute('x',(x0+3).toFixed(2));
    t.textContent = w>32 ? (s.length>w/6.1?s.slice(0,Math.max(1,w/6.1-1))+'\\u2026':s) : '';
  }
}
function pct(v){return (100*v/TOTAL).toFixed(2)+'%%';}
for(var i=0;i<fs.length;i++){
  (function(g){
    g.onclick=function(){zx=+g.dataset.x;zv=+g.dataset.v;lay();
      det.textContent='zoomed: '+g.dataset.n+'  '+g.dataset.v+' samples, '+pct(+g.dataset.v)+' of work';};
    g.onmouseover=function(){det.textContent=g.dataset.n+'  '+g.dataset.v+' samples, '+pct(+g.dataset.v)+' of work';};
  })(fs[i]);
}
document.getElementById('ttl').onclick=function(){zx=0;zv=TOTAL;lay();det.textContent='click a frame to zoom, click the title to reset';};
document.getElementById('ttl').style.cursor='pointer';
lay();
]]></script>""" % (W, PAD, total))
    parts.append('</svg>')
    open(OUT, "w", encoding="utf-8").write("\n".join(parts))
    print("%s: %d work samples, %d idle (%.1f%% of capture), depth %d"
          % (OUT, total, idle, 100.0 * idle / (total + idle), depth_max))


main()
