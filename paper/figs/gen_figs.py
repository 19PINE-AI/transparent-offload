#!/usr/bin/env python3
"""Generate all figures for 'Fine-Grained Computation Offload for Off-the-Shelf Servers
in Tens of Lines'.
Clean, single-column, NeurIPS-ish style. Vector PDF output."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle
import numpy as np
import os

OUT = os.path.dirname(os.path.abspath(__file__))

# ---- palette (matches the paper's slate-blue accent) ----
INK    = "#1A1A2E"
SLATE  = "#34507F"   # primary / single event loop
TEAL   = "#2A9D8F"   # event loop + pool
GREEN  = "#4E8B3B"   # thread / goroutine pool
AMBER  = "#E08A1E"   # process-per-connection
PURPLE = "#7B5EA7"   # proxy
GRAY   = "#9AA3B2"
RED    = "#B0241B"   # walls / corruption
LIGHT  = "#EAEDF3"

plt.rcParams.update({
    "font.family": "sans-serif",
    "font.sans-serif": ["DejaVu Sans"],
    "font.size": 11,
    "axes.edgecolor": "#444444",
    "axes.linewidth": 0.9,
    "axes.titlesize": 12,
    "axes.labelsize": 11.5,
    "xtick.color": INK, "ytick.color": INK,
    "text.color": INK, "axes.labelcolor": INK,
    "axes.spines.top": False, "axes.spines.right": False,
    "figure.dpi": 130,
    "savefig.bbox": "tight", "savefig.pad_inches": 0.03,
})

def save(fig, name):
    fig.savefig(os.path.join(OUT, name + ".pdf"))
    plt.close(fig)
    print("wrote", name + ".pdf")

def box(ax, x, y, w, h, fc, ec=INK, text="", fs=10, tc=INK, lw=1.2, round=0.02, bold=False):
    p = FancyBboxPatch((x, y), w, h, boxstyle=f"round,pad=0.002,rounding_size={round}",
                       fc=fc, ec=ec, lw=lw, zorder=2)
    ax.add_patch(p)
    if text:
        ax.text(x + w/2, y + h/2, text, ha="center", va="center", fontsize=fs,
                color=tc, zorder=3, fontweight="bold" if bold else "normal")

def arrow(ax, p0, p1, color=INK, lw=1.6, style="-|>", ms=10, rad=0.0, ls="-"):
    ax.add_patch(FancyArrowPatch(p0, p1, arrowstyle=style, mutation_scale=ms,
                 lw=lw, color=color, connectionstyle=f"arc3,rad={rad}", zorder=4, linestyle=ls))


# =====================================================================
# Fig 1 — the offload stall and overlap (gantt)
# =====================================================================
def fig_pipeline():
    fig, axes = plt.subplots(2, 1, figsize=(6.4, 3.2), gridspec_kw=dict(hspace=0.55))
    # (recv, pre, OFFLOAD, post, send) durations
    def seg(ax, y, segs, label):
        x = 0
        for (dur, col, txt, hatch) in segs:
            ax.add_patch(Rectangle((x, y), dur, 0.6, fc=col, ec=INK, lw=1.0,
                         hatch=hatch, zorder=2))
            if txt:
                # on a hatched segment, back the label with a white pill so the
                # slashes do not run across the text
                bbox = (dict(boxstyle="round,pad=0.22", fc="white", ec="none", alpha=0.9)
                        if hatch else None)
                ax.text(x + dur/2, y + 0.3, txt, ha="center", va="center",
                        fontsize=8.5, color=INK if col != SLATE else "white",
                        fontweight="bold", zorder=3, bbox=bbox)
            x += dur
        return x
    CPU = SLATE; IDLE = LIGHT; ACC = AMBER
    # Top: synchronous — CPU idle during offload
    ax = axes[0]
    seg(ax, 0, [(1,CPU,"recv",None),(1,CPU,"pre",None),(5,IDLE,"accelerator busy","//"),
                (1,CPU,"post",None),(1,CPU,"send",None)], "sync")
    ax.text(4.5, 0.82, "CPU idle (wasted)", ha="center", va="center", fontsize=9.5, color=RED, fontweight="bold")
    ax.set_xlim(0, 9.4); ax.set_ylim(-0.15, 1.05)
    ax.set_title("Synchronous offload: one request, CPU stalls", loc="left", fontsize=11, color=INK)
    # Bottom: overlapped — other requests fill the gap
    ax = axes[1]
    # request A
    seg(ax, 0.7, [(1,CPU,"A:recv",None),(1,CPU,"A:pre",None),(5,LIGHT,"A: offloaded (parked)","//"),
                  (1,CPU,"A:post",None)], "A")
    # request B,C run on the CPU during A's offload
    x=2
    for (lab,col) in [("B",TEAL),("C",GREEN),("B",TEAL)]:
        ax.add_patch(Rectangle((x,0),1.6,0.6,fc=col,ec=INK,lw=1.0,zorder=2))
        ax.text(x+0.8,0.3,lab,ha="center",va="center",fontsize=8.5,color="white",fontweight="bold")
        x+=1.7
    ax.set_xlim(0, 9.4); ax.set_ylim(-0.15, 1.5)
    ax.set_title("Overlapped offload: CPU serves B, C while A's offload is in flight", loc="left",
                 fontsize=11, color=INK)
    for ax in axes:
        ax.set_yticks([]); ax.set_xticks([])
        for s in ax.spines.values(): s.set_visible(False)
    axes[1].annotate("time", xy=(9.2,-0.05), xytext=(8.2,-0.05), fontsize=9,
                     arrowprops=dict(arrowstyle="-|>", color=INK), va="center")
    save(fig, "fig_pipeline")


# =====================================================================
# Fig 2 — the spectrum: lines vs speedup (ANCHOR)
# =====================================================================
def fig_spectrum():
    # REAL-hardware speedups only (real GPU). Servers whose dramatic numbers needed a
    # latency-bound offload are shown by line-count at the baseline (integration built;
    # measured at y=1 on the real-GPU AES path or pending a real remote offload).
    pts = [   # (app, lines_added, speedup, regime_color, dx, dy, ha) -- real GPU AES
        ("Redis", 83, 3.01, SLATE, 9, 2, "left"),
        ("Node.js", 34, 2.54, SLATE, 0, -14, "center"),
        ("Python", 22, 2.37, SLATE, -9, -9, "right"),
        ("nginx", 112, 2.74, TEAL, 0, -14, "center"),
        ("memcached", 70, 2.93, TEAL, 0, 10, "center"),
        ("Go", 28, 3.01, GREEN, -9, 3, "right"),
        ("Apache", 27, 3.45, GREEN, 0, 9, "center"),
        ("Postgres", 42, 2.59, AMBER, 9, -3, "left"),
        ("MariaDB", 34, 2.59, AMBER, -9, 0, "right"),   # nearly coincides with Node.js
        ("HAProxy", 138, 2.10, PURPLE, 0, -15, "center"),   # standalone C SPOE agent
    ]
    fig, ax = plt.subplots(figsize=(6.6, 4.0))
    for (name, x, y, c, dx, dy, ha) in pts:
        ax.scatter(x, y, s=120, color=c, edgecolor="white", lw=1.3, zorder=3)
        ax.annotate(name, (x, y), textcoords="offset points", xytext=(dx, dy),
                    fontsize=9.5, fontweight="bold", color=INK, ha=ha)
    # transparent runtime at x≈1 (log floor), distinct hollow star
    ax.scatter(1.4, 17.3, s=240, marker="*", facecolor="white", edgecolor=GRAY, lw=1.8, zorder=3)
    ax.annotate("transparent\n(0 edits, own binary;\nreal GPU AES)", (1.4, 17.3),
                textcoords="offset points", xytext=(34, 0), fontsize=8.5, color="#5b6577",
                ha="left", va="center")
    # the device-throughput ceiling that bounds a single compute-bound accelerator
    ax.axhline(1.0, color="#999", ls="--", lw=1.0, zorder=0)
    ax.text(150, 1.03, "no gain", fontsize=7.5, color="#777", va="bottom", ha="right")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlim(1, 200); ax.set_ylim(0.8, 22)
    ax.set_xlabel("lines added to the application  (0–138)")
    ax.set_ylabel("speedup over sync offload  (×, real GPU)")
    ax.grid(True, which="both", ls=":", lw=0.6, color="#cfd5e0", zorder=0)
    ax.set_xticks([1, 10, 100]); ax.set_xticklabels(["~0", "10", "100"])
    ax.set_yticks([1,2,3,5,10,20]); ax.set_yticklabels(["1","2","3","5","10","20"])
    handles = [mpatches.Patch(color=SLATE, label="single event loop"),
               mpatches.Patch(color=TEAL, label="event loop + pool"),
               mpatches.Patch(color=GREEN, label="thread / goroutine pool"),
               mpatches.Patch(color=PURPLE, label="proxy (offload agent)"),
               mpatches.Patch(color=AMBER, label="per-connection DB (intra-query)")]
    ax.legend(handles=handles, fontsize=7.6, loc="upper right", frameon=True, framealpha=0.96)
    save(fig, "fig_spectrum")




# =====================================================================
# Fig 4 — concurrency-model regimes and the predicted win (schematic)
# =====================================================================
def fig_regimes():
    fig, ax = plt.subplots(figsize=(6.5, 2.95))
    ax.set_xlim(0, 10); ax.set_ylim(2.95, 10.2); ax.axis("off")
    cols = [
        ("single\nevent loop", SLATE, "sync offload\nstalls everything", "2.4–3.0×", "Redis, Node,\nPython"),
        ("event loop\n+ pool", TEAL, "stalls one\nloop of N", "2.7–2.9×", "nginx, memcached"),
        ("thread /\ngoroutine\npool", GREEN, "pool overlaps\nthe rest", "3.0–3.5×\n(0 async code)", "Apache, Go"),
        ("proxy +\nagent", PURPLE, "agent offloads\nasync", "2.1×\n(C agent)", "HAProxy"),
        ("process-per-\nconnection", AMBER, "OS overlaps\nconnections free", "intra-query\npipelining", "Postgres, MariaDB"),
    ]
    n = len(cols); w = 1.78; gap = 0.16; x0 = 0.15
    for i,(title,col,what,win,ex) in enumerate(cols):
        x = x0 + i*(w+gap)
        box(ax, x, 6.7, w, 2.7, fc=col, ec=INK, text="", round=0.08)
        ax.text(x+w/2, 8.55, title, ha="center", va="center", color="white", fontsize=8.4, fontweight="bold")
        ax.text(x+w/2, 7.35, what, ha="center", va="center", color="white", fontsize=7.0)
        box(ax, x, 4.6, w, 1.8, fc="white", ec=col, text="", round=0.08, lw=1.6)
        ax.text(x+w/2, 5.5, win, ha="center", va="center", color=INK, fontsize=8.2, fontweight="bold")
        ax.text(x+w/2, 4.05, ex, ha="center", va="center", color="#5b6577", fontsize=7.3, style="italic")
    ax.text(5.0, 10.0, "concurrency model  →  predicted rerouting win", ha="center",
            fontsize=11, fontweight="bold", color=INK)
    ax.text(5.0, 3.2, "Win grows with offload weight; it pays only when the offload outweighs per-request CPU.",
            ha="center", fontsize=8.6, color="#5b6577", style="italic")
    save(fig, "fig_regimes")


# =====================================================================
# Fig 5 — transparent M:N fiber runtime (schematic)
# =====================================================================
def fig_runtime():
    fig, ax = plt.subplots(figsize=(6.4, 2.75))
    ax.set_xlim(0, 10); ax.set_ylim(0, 8.9); ax.axis("off")
    # carrier core box containing fibers + scheduler
    box(ax, 0.4, 1.2, 5.2, 7.4, fc=LIGHT, ec=SLATE, text="", round=0.04, lw=1.6)
    ax.text(3.0, 8.1, "carrier core (one OS thread)", ha="center", fontsize=10, fontweight="bold", color=SLATE)
    fibs = [("fiber A  (conn 1)", 6.6, TEAL), ("fiber B  (conn 2)", 5.3, GREEN), ("fiber C  (conn 3)", 4.0, PURPLE)]
    for (t,y,c) in fibs:
        box(ax, 0.8, y, 4.4, 1.0, fc="white", ec=c, text=t, fs=9, lw=1.5, round=0.06)
    box(ax, 0.8, 1.5, 4.4, 1.9, fc=SLATE, ec=INK, text="scheduler\n(epoll + poll device)", fs=9, tc="white", round=0.06)
    # accelerator device
    box(ax, 7.0, 4.4, 2.5, 2.0, fc=AMBER, ec=INK, text="accelerator\ndevice", fs=10, tc="white", round=0.06)
    arrow(ax, (5.2, 5.0), (7.0, 5.6), color=AMBER, rad=0.2)
    ax.text(6.1, 6.2, "offload\n(submit + yield)", ha="center", fontsize=8.2, color=INK)
    arrow(ax, (7.0, 4.9), (5.2, 2.6), color=SLATE, rad=0.2, ls="-")
    ax.text(6.1, 3.3, "completion\n(resume fiber)", ha="center", fontsize=8.2, color=INK)
    # LD_PRELOAD note (tucked just under the carrier box)
    ax.set_ylim(-0.55, 8.9)
    ax.text(5.0, 0.05, r"$\bf{LD\_PRELOAD}$ interposes pthread_create$\to$fiber, read/write/poll$\to$yield, offload$\to$yield."
            "\nThe application binary is unchanged.", ha="center", fontsize=8.6, color="#5b6577")
    save(fig, "fig_runtime")


# =====================================================================
# Fig 6 — three walls where transparency stops (schematic)
# =====================================================================
def fig_walls():
    fig, ax = plt.subplots(figsize=(6.5, 3.6))
    ax.set_xlim(0, 12); ax.set_ylim(-0.4, 9); ax.axis("off")
    # three layers (kept clean)
    box(ax, 0.4, 6.6, 11.2, 1.7, fc=LIGHT, ec=INK, text="", round=0.03)
    ax.text(2.9, 7.45, "Application / runtime", ha="center", va="center", fontsize=10, color=INK, fontweight="bold")
    box(ax, 0.4, 4.2, 11.2, 1.7, fc="#DCE6F2", ec=SLATE, text="libc symbols  —  the interposition layer",
        fs=9.5, round=0.03, lw=1.8)
    box(ax, 0.4, 1.8, 11.2, 1.5, fc=LIGHT, ec=INK, text="Kernel / hardware", fs=10, round=0.03)
    ax.text(6.0, 8.6, "Where transparent interposition reaches — and the three walls", ha="center",
            fontsize=10.5, fontweight="bold", color=INK)
    # wall 1: raw futex bypasses libc (arrow from app, around libc, to kernel)
    arrow(ax, (1.5, 6.6), (1.5, 3.3), color=RED, lw=2.2, rad=0.0)
    ax.text(1.5, 1.35, "raw syscall(futex)\nbelow libc\n(InnoDB)", ha="center", va="top", fontsize=8, color=RED, fontweight="bold")
    # wall 2: native TLS register (lives in the app/runtime; not virtualizable)
    box(ax, 6.0, 6.75, 1.7, 1.4, fc="white", ec=RED, text="%fs\nTLS", fs=8.5, tc=RED, lw=1.8, round=0.06)
    ax.text(6.85, 1.35, "thread identity in\nnative TLS register\n(JVM, Go)", ha="center", va="top", fontsize=8, color=RED, fontweight="bold")
    # wall 3: no idle cpu
    box(ax, 9.4, 6.75, 1.7, 1.4, fc="white", ec=RED, text="CPU\n100%", fs=8.5, tc=RED, lw=1.8, round=0.06)
    ax.text(10.25, 1.35, "per-request CPU\n> offload\n(TLS crypto)", ha="center", va="top", fontsize=8, color=RED, fontweight="bold")
    save(fig, "fig_walls")


# =====================================================================
# Fig 7 — overlap pays only when the offload outweighs per-request work
# =====================================================================
def fig_weight():
    # MEASURED (idle GPU): real GPU AES block-size sweep on the single-event-loop
    # Python server (apps/aes_blocksize_py_results.csv). Block size = offload weight.
    kb  = [4,8,16,32,64,128,256,512,1024,2048,4096,8192]
    lat = [46.4,47.0,51.4,60.3,70.2,92.7,126.5,191.8,361.6,653.6,1188.1,2258.6]
    spd = [1.24,1.26,1.25,1.25,1.31,1.42,1.53,1.83,2.37,2.96,3.83,5.41]
    fig, ax = plt.subplots(figsize=(6.6, 3.4))
    ax.plot(kb, spd, "-o", color=TEAL, lw=2.3, ms=6, label="speedup (async/sync)")
    ax.set_xscale("log", base=2); ax.set_ylim(1.0, 5.9)
    ax.set_xlabel("AES block size (offload weight)")
    ax.set_ylabel("speedup  (×)", color=TEAL)
    ax.axhline(1.0, color="#999", ls="--", lw=1.0)
    ax.text(300, 1.05, "no benefit (offload $\\approx$ per-request work)", fontsize=7.6,
            color="#777", ha="center", va="bottom")
    ax.set_xticks(kb); ax.set_xticklabels(["4K","8K","16K","32K","64K","128K","256K","512K","1M","2M","4M","8M"], fontsize=7.5)
    ax2 = ax.twinx()                                   # real single-op GPU latency
    ax2.plot(kb, lat, "-s", color=AMBER, lw=1.8, ms=5, label="GPU latency")
    ax2.set_yscale("log"); ax2.set_ylabel("single-op GPU latency  (µs)", color=AMBER)
    ax.grid(True, which="both", ls=":", lw=0.5, color="#cfd5e0")
    ax.set_title("Overlap pays with offload weight (real GPU AES, single event loop)", loc="left", fontsize=10)
    ax.legend(loc="upper left", fontsize=8.5, frameon=True)
    ax2.legend(loc="lower right", fontsize=8.5, frameon=True)
    save(fig, "fig_weight")


# =====================================================================
# Fig 8 — correctness: overlap can corrupt shared state; the detector fixes it
# =====================================================================
def fig_correctness():
    fig, axes = plt.subplots(1, 2, figsize=(6.6, 3.0), gridspec_kw=dict(wspace=0.45, width_ratios=[1,1]))
    # left: lost updates (measured on stock Redis, real GPU offload)
    ax = axes[0]
    labels = ["naive\n(overlap)", "detector\n+ enforce"]
    lost = [23450, 0]
    # linear scale: zero is honestly zero (a log axis would draw it as 10^0 = 1)
    ax.bar(labels, lost, color=[RED, GREEN], ec=INK, lw=1.0, width=0.6)
    ax.set_ylim(0, 23450*1.45)
    ax.set_ylabel("lost updates")
    ax.text(0, 23450*1.05, "23,450\nlost", ha="center", fontsize=9.5, color=RED, fontweight="bold")
    ax.text(1, 900, "0\ncorrect", ha="center", fontsize=9.5, color=GREEN, fontweight="bold")
    ax.set_title("Shared-state safety (stock Redis)", fontsize=9.5)
    ax.grid(True, axis="y", ls=":", lw=0.5, color="#cfd5e0")
    # right: naive vs detector vs coarse lock throughput (real Redis, two contention levels)
    ax = axes[1]
    naive = [24.06, 22.78]; det = [24.69, 23.94]; lock = [13.81, 14.24]
    x = np.arange(2); w=0.26
    ax.bar(x-w, naive, w, color=GRAY,  ec=INK, lw=1.0, label="naive (unsafe)")
    ax.bar(x,   det,   w, color=SLATE, ec=INK, lw=1.0, label="detector (overlapped)")
    ax.bar(x+w, lock,  w, color=AMBER, ec=INK, lw=1.0, label="lock (serialized)")
    ax.set_ylim(0, 42)
    ax.set_xticks(x); ax.set_xticklabels(["high\ncontention","low\ncontention"])
    ax.set_ylabel("throughput  (K req/s)")
    ax.text(0, 26.0, "1.8×", ha="center", fontsize=10, color=SLATE, fontweight="bold")
    ax.text(1, 25.2, "1.7×", ha="center", fontsize=10, color=SLATE, fontweight="bold")
    ax.legend(fontsize=7.0, loc="upper right", frameon=True, framealpha=0.95)
    ax.set_title("Detector keeps offloads overlapped", fontsize=9.5)
    ax.grid(True, axis="y", ls=":", lw=0.5, color="#cfd5e0")
    save(fig, "fig_correctness")






# =====================================================================
# Fig 11 — a classifier: futex density predicts fiberizability
# =====================================================================
def fig_classifier():
    fig, ax = plt.subplots(figsize=(6.4, 3.0))
    apps = ["Redis", "stunnel", "memcached", "MariaDB\n(InnoDB)"]
    futex = [25, 24, 1258, 12966]
    # stunnel: thread-per-conn, libc-level blocking -> fiberizable (teal).
    # Redis/memcached: event-driven (epoll profile) -> loads safely, no overlap (gray).
    # MariaDB/InnoDB: raw futex below libc + io_uring -> the wall (red).
    colors = [GRAY, TEAL, GRAY, RED]
    bars = ax.bar(apps, futex, color=colors, ec=INK, lw=1.0, width=0.6)
    ax.set_yscale("log"); ax.set_ylim(8, 60000)
    ax.set_ylabel("per-request futex calls  (log)")
    ax.text(3, 12966*1.25, "sub-libc wall\n(+ io_uring)", ha="center", fontsize=8.5, color=RED,
            va="bottom", fontweight="bold")
    ax.text(1, 24*1.5, "fiberizable", fontsize=8.5, color=TEAL, ha="center", va="bottom",
            fontweight="bold")
    handles = [mpatches.Patch(color=TEAL, label="fiberizable (libc-level blocking)"),
               mpatches.Patch(color=GRAY, label="event-driven (loads safely, no overlap)"),
               mpatches.Patch(color=RED,  label="sub-libc wall")]
    ax.legend(handles=handles, fontsize=7.2, loc="upper left", frameon=True, framealpha=0.95)
    ax.grid(True, axis="y", ls=":", lw=0.5, color="#cfd5e0")
    ax.set_title("Futex density predicts whether a binary can be fiberized", loc="left", fontsize=10.5)
    save(fig, "fig_classifier")


# =====================================================================
# Fig 12 — the rerouting recipe (schematic)
# =====================================================================
def fig_recipe():
    fig, ax = plt.subplots(figsize=(6.5, 2.5))
    ax.set_xlim(0, 12); ax.set_ylim(1.55, 7.0); ax.axis("off")
    # the handler reaches the offload
    box(ax, 0.3, 4.6, 2.4, 1.6, fc=SLATE, ec=INK, text="handler reaches\nthe offload", fs=9, tc="white", round=0.07)
    # step 1: submit
    box(ax, 3.9, 4.9, 3.5, 1.5, fc="white", ec=GREEN, text="1. submit to a\nbackground executor", fs=8.5, lw=1.6, round=0.07)
    arrow(ax, (2.7, 5.4), (3.9, 5.6), color=GREEN)
    # device
    box(ax, 8.5, 4.9, 3.2, 1.5, fc=AMBER, ec=INK, text="accelerator runs\nthe offload", fs=8.5, tc="white", round=0.07)
    arrow(ax, (7.4, 5.65), (8.5, 5.65), color=AMBER)
    # step 2: suspend
    box(ax, 3.9, 2.6, 3.5, 1.5, fc="white", ec=PURPLE, text="2. suspend the request\n(server's own primitive)", fs=8.5, lw=1.6, round=0.07)
    arrow(ax, (1.5, 4.6), (4.1, 4.1), color=PURPLE, rad=-0.2)
    # event loop stays free
    box(ax, 0.3, 2.5, 2.4, 1.5, fc=LIGHT, ec=TEAL, text="loop serves\nother requests", fs=8.5, lw=1.5, round=0.07)
    arrow(ax, (2.7, 3.2), (3.9, 3.3), color=TEAL, ls="--")
    # step 3: resume
    box(ax, 8.5, 2.6, 3.2, 1.5, fc="white", ec=SLATE, text="3. resume + reply\non completion", fs=8.5, lw=1.6, round=0.07)
    arrow(ax, (10.1, 4.9), (10.1, 4.1), color=SLATE)
    arrow(ax, (8.5, 3.35), (7.4, 3.35), color=SLATE, ls="-")
    ax.text(6.0, 6.72, "The recipe: reroute the offload through the server's existing suspend/resume machinery",
            ha="center", fontsize=10, fontweight="bold", color=INK)
    ax.text(6.0, 1.85, "22–138 lines added, at most 1 modified — the machinery already exists; one only reroutes the offload.",
            ha="center", fontsize=8.7, color="#5b6577", style="italic")
    save(fig, "fig_recipe")


# =====================================================================
# Fig 13 — the glibc cond-versioning hazard across binaries
# =====================================================================
def fig_condsweep():
    bins = ["MariaDB", "Redis", "memcached", "nginx", "stunnel"]
    # 0 = OK (green), 1 = crash (red)
    unver = [1, 0, 0, 0, 0]
    ver   = [0, 0, 0, 0, 0]
    fig, ax = plt.subplots(figsize=(6.3, 2.4))
    rows = [("naive\n(unversioned)", unver, 1.0), ("versioned\n(our fix)", ver, 0.0)]
    for ri,(label, vals, yy) in enumerate(rows):
        for ci,(v) in enumerate(vals):
            col = RED if v else GREEN
            txt = "CRASH" if v else "OK"
            box(ax, ci*2.2, yy, 2.0, 0.85, fc=col, ec="white", text=txt, fs=9, tc="white", round=0.05, lw=1.0)
        ax.text(-0.25, yy+0.42, label, ha="right", va="center", fontsize=9, fontweight="bold")
    for ci,b in enumerate(bins):
        ax.text(ci*2.2+1.0, 2.15, b, ha="center", va="center", fontsize=9.5, fontweight="bold", color=INK)
    ax.set_xlim(-2.6, len(bins)*2.2); ax.set_ylim(-0.3, 2.4); ax.axis("off")
    ax.text((len(bins)*2.2)/2-1.3, 2.55, "Interposing pthread_cond_* without version-matching",
            ha="center", fontsize=10, fontweight="bold", color=INK)
    save(fig, "fig_condsweep")


# =====================================================================
# Fig 14 — accelerator latency landscape (the fine-grained regime)
# =====================================================================
def fig_landscape():
    fig, ax = plt.subplots(figsize=(6.5, 2.7))
    ax.set_xscale("log")
    ax.set_xlim(2e2, 3e7)   # nanoseconds: 0.2us .. 30ms
    ax.set_ylim(0, 3.7)
    # fine-grained band: ~1us .. ~1ms
    ax.axvspan(1e3, 1e6, color="#f0ece0", zorder=0)
    ax.text(3.2e4, 3.45, "fine-grained regime: offload $\\approx$ scheduling cost",
            ha="center", fontsize=9.5, color="#7a6a3a", style="italic")
    # all labels ABOVE the marker line, staggered at two heights to clear the x-axis ticks
    items = [
        (2.5e3, "OS context switch\n(~1–5 µs)", RED, "lo"),
        (3e4,   "GPU AES /\nsmall kernel\n(~10–50 µs)", SLATE, "hi"),
        (2.5e5, "compression /\nsmall inference\n(~0.1–0.5 ms)", TEAL, "lo"),
        (3e6,   "HSM sign · PQC KEM /\nremote inference\n(~1–10 ms)", AMBER, "hi"),
    ]
    ax.axhline(0.45, color="#888", lw=1.2, zorder=1)
    for (x, lab, col, lvl) in items:
        ax.plot([x],[0.45], "o", ms=11, color=col, zorder=3)
        dy = 70 if lvl == "hi" else 26
        ax.annotate(lab, (x, 0.45), textcoords="offset points", xytext=(0, dy),
                    ha="center", va="bottom", fontsize=8.5, color=INK,
                    arrowprops=dict(arrowstyle="-", color=col, lw=1.0))
    ax.set_yticks([])
    ax.set_xlabel("offload latency  (log scale)")
    ax.set_xticks([1e3,1e4,1e5,1e6,1e7]); ax.set_xticklabels(["1 µs","10 µs","100 µs","1 ms","10 ms"])
    for s in ["left","right","top"]: ax.spines[s].set_visible(False)
    save(fig, "fig_landscape")


# =====================================================================
# Fig 15 — latency vs offered load (overlap sustains low latency further)
# =====================================================================
def fig_latency():
    # Measured open-loop data (Poisson arrivals) from runtime/openloop.csv.
    # Offered load in K req/s; latency in us. Block's median knee is ~103K, the
    # overlap path's is ~410K -> overlap holds low latency to ~4x the load.
    co = dict(off=[51.4, 102.7, 205.4, 308.3, 410.7, 462.3],
              p50=[22.8, 22.9, 23.3, 24.6, 32.2, 86919.4],
              p99=[4243.4, 19778.4, 19660.3, 30084.2, 43123.4, 97054.8])
    bl = dict(off=[50.7, 102.9, 205.4, 216.7, 250.4, 275.9],
              p50=[25.4, 27.7, 259500.0, 337506.9, 336862.1, 186429.1],
              p99=[11649.2, 25898.4, 325811.6, 388513.3, 379708.5, 385242.2])
    fig, axes = plt.subplots(1, 2, figsize=(6.8, 3.0), gridspec_kw=dict(wspace=0.34))
    for ax, key, title in [(axes[0], 'p50', 'median (p50)'),
                           (axes[1], 'p99', 'tail (p99)')]:
        ax.plot(co['off'], co[key], "-o", color=SLATE, lw=2.2, ms=6, label="overlap (fibers)")
        ax.plot(bl['off'], bl[key], "-s", color=RED,   lw=2.2, ms=6, label="block (thread pool)")
        ax.set_yscale("log"); ax.set_ylim(15, 6e5); ax.set_xlim(40, 470)
        ax.set_xlabel("offered load  (K req/s)")
        ax.set_ylabel(f"{key} latency  (µs, log)")
        ax.axvline(103, color=RED,   ls=":", lw=1.1)
        ax.axvline(410, color=SLATE, ls=":", lw=1.1)
        ax.grid(True, which="both", ls=":", lw=0.5, color="#cfd5e0")
        ax.set_title(title, loc="left", fontsize=10)
    axes[0].annotate("", xy=(410, 42), xytext=(103, 42),
                     arrowprops=dict(arrowstyle="<->", color=INK))
    axes[0].text(256, 27, "~4× the load at\nthe same latency", ha="center",
                 fontsize=8.0, color=INK)
    # legend outside the plots (below) so it never overlaps the curves
    h, l = axes[0].get_legend_handles_labels()
    fig.legend(h, l, loc="upper center", bbox_to_anchor=(0.5, -0.02), ncol=2,
               fontsize=8.6, frameon=True)
    fig.suptitle("Overlap holds low latency to ~4× the offered load (open-loop, Poisson arrivals)",
                 fontsize=10.5, y=1.02)
    save(fig, "fig_latency")


# =====================================================================
# Fig 16 — positioning vs prior work
# =====================================================================
def fig_positioning():
    fig, ax = plt.subplots(figsize=(6.4, 3.6))
    ax.set_xlim(0, 10); ax.set_ylim(0, 10)
    ax.set_xlabel("modification to an existing server  →")
    ax.set_ylabel("breadth of servers it applies to  →")
    ax.set_xticks([]); ax.set_yticks([])
    # our coverage region: low modification, broad
    ax.add_patch(FancyBboxPatch((0.3, 4.3), 6.6, 5.0, boxstyle="round,pad=0.02,rounding_size=0.2",
                 fc="#dce6f2", ec=SLATE, lw=1.8, alpha=0.55, zorder=1))
    ax.text(3.6, 9.0, "This paper: rerouting via\nexisting concurrency\n(0–138 lines, 10 server types)",
            ha="center", va="top", fontsize=9.5, color=SLATE, fontweight="bold", zorder=3)
    def pt(x,y,label,c,dx=0,dy=10,ha="center"):
        ax.scatter(x,y,s=90,color=c,edgecolor="white",lw=1.2,zorder=4)
        ax.annotate(label,(x,y),textcoords="offset points",xytext=(dx,dy),ha=ha,fontsize=8.5,color=INK)
    pt(1.0, 1.5, "transparent\nthreading libs", GRAY, -2, 12, "center")  # need to write app that way
    pt(9.0, 1.3, "rewrite in an\nasync framework", PURPLE, 0, 14, "center")
    pt(5.8, 2.6, "point offload\nintegrations\n(QAT, SSLShader)", AMBER, -6, 0, "right")
    pt(2.1, 5.0, "our transparent\nruntime (one corner\nof the spectrum)", SLATE, 16, 0, "left")
    ax.grid(True, ls=":", lw=0.5, color="#dfe3ea")
    ax.set_title("Positioning: low modification AND broad coverage", loc="left", fontsize=10.5)
    save(fig, "fig_positioning")




# =====================================================================
# Headline — per-server speedup over synchronous offload (page-1 teaser)
# =====================================================================
def fig_headline():
    # Same real-GPU AES speedups as the spectrum (Fig. fig_spectrum), shown as a
    # compact bar chart so it can ride on the abstract page as the headline result.
    # (1 MiB blocks for all ten; the two databases pipeline the same op intra-query.
    # DB source: transparent-runtime/apps/db_intraquery_gpu.csv.)
    data = [   # (app, speedup, label, regime_color)
        ("Apache",    3.45, "3.45×", GREEN),
        ("Redis",     3.01, "3.01×", SLATE),
        ("Go",        3.01, "3.01×", GREEN),
        ("memcached", 2.93, "2.93×", TEAL),
        ("nginx",     2.74, "2.74×", TEAL),
        ("MariaDB",   2.59, "2.59×", AMBER),
        ("Postgres",  2.59, "2.59×", AMBER),
        ("Node.js",   2.54, "2.54×", SLATE),
        ("Python",    2.37, "2.37×", SLATE),
        ("HAProxy",   2.10, "2.10×", PURPLE),
    ]
    data = data[::-1]   # largest on top
    names = [d[0] for d in data]; vals = [d[1] for d in data]
    labs = [d[2] for d in data]; cols = [d[3] for d in data]
    fig, ax = plt.subplots(figsize=(6.6, 1.5))
    y = np.arange(len(names))
    ax.barh(y, vals, color=cols, ec=INK, lw=0.9, height=0.68, zorder=3)
    ax.axvline(1.0, color="#999", ls="--", lw=1.0, zorder=2)
    for yi, v, lab in zip(y, vals, labs):
        ax.text(v + 0.05, yi, lab, va="center", ha="left",
                fontsize=7.8, fontweight="bold", color=INK)
    ax.set_yticks(y); ax.set_yticklabels(names, fontsize=8.2)
    ax.set_xlim(0, 4.05); ax.set_ylim(-0.6, len(names)-0.4)
    ax.set_xlabel("speedup over synchronous offload  (×, real GPU AES)", fontsize=9)
    ax.grid(True, axis="x", ls=":", lw=0.5, color="#cfd5e0", zorder=0)
    handles = [mpatches.Patch(color=SLATE,  label="event loop"),
               mpatches.Patch(color=TEAL,   label="loop + pool"),
               mpatches.Patch(color=GREEN,  label="thread pool"),
               mpatches.Patch(color=PURPLE, label="proxy"),
               mpatches.Patch(color=AMBER,  label="per-conn DB")]
    # legend tucked into the empty bottom-right corner so it never covers a bar
    ax.legend(handles=handles, fontsize=6.3, loc="lower right", frameon=True,
              framealpha=0.95, borderpad=0.35, labelspacing=0.28,
              handlelength=0.95, handletextpad=0.4)
    save(fig, "fig_headline")


if __name__ == "__main__":
    fig_pipeline(); fig_spectrum(); fig_regimes(); fig_runtime()
    fig_walls(); fig_weight(); fig_correctness(); fig_classifier()
    fig_recipe(); fig_condsweep(); fig_landscape(); fig_latency(); fig_positioning()
    fig_headline()
    print("ALL FIGURES DONE")
