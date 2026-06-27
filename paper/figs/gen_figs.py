#!/usr/bin/env python3
"""Generate all figures for 'Fine-Grained Computation Offload for Event-Driven Applications'.
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
                ax.text(x + dur/2, y + 0.3, txt, ha="center", va="center",
                        fontsize=8.5, color=INK if col != SLATE else "white",
                        fontweight="bold")
            x += dur
        return x
    CPU = SLATE; IDLE = LIGHT; ACC = AMBER
    # Top: synchronous — CPU idle during offload
    ax = axes[0]
    seg(ax, 0, [(1,CPU,"recv",None),(1,CPU,"pre",None),(5,IDLE,"accelerator busy","////"),
                (1,CPU,"post",None),(1,CPU,"send",None)], "sync")
    ax.text(4.5, 0.82, "CPU idle (wasted)", ha="center", va="center", fontsize=9.5, color=RED, fontweight="bold")
    ax.set_xlim(0, 9.4); ax.set_ylim(-0.15, 1.05)
    ax.set_title("Synchronous offload: one request, CPU stalls", loc="left", fontsize=11, color=INK)
    # Bottom: overlapped — other requests fill the gap
    ax = axes[1]
    # request A
    seg(ax, 0.7, [(1,CPU,"A:recv",None),(1,CPU,"A:pre",None),(5,LIGHT,"A: offloaded (parked)","////"),
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
    # (app, lines_added, speedup, regime_color, label_dx, label_dy, ha)
    pts = [
        ("Redis", 83, 31.6, SLATE, -8, 8, "right"),
        ("Node.js", 33, 24.8, SLATE, 0, 12, "center"),
        ("Python", 22, 13.8, SLATE, -8, 4, "right"),
        ("nginx", 112, 5.2, TEAL, -9, 5, "right"),
        ("memcached", 70, 3.8, TEAL, 0, -15, "center"),
        ("Apache", 27, 59, GREEN, 0, 13, "center"),
        ("Go", 28, 59, GREEN, 0, -17, "center"),
        ("Postgres", 30, 141, AMBER, -9, 2, "right"),
        ("MariaDB", 34, 212, AMBER, 9, 2, "left"),
        ("HAProxy", 18, 13.3, PURPLE, 0, -15, "center"),
    ]
    fig, ax = plt.subplots(figsize=(6.6, 4.1))
    for (name, x, y, c, dx, dy, ha) in pts:
        ax.scatter(x, y, s=120, color=c, edgecolor="white", lw=1.3, zorder=3)
        ax.annotate(name, (x, y), textcoords="offset points", xytext=(dx, dy),
                    fontsize=9.5, fontweight="bold", color=INK, ha=ha)
    # transparent runtime at x≈1 (log floor), distinct hollow star
    ax.scatter(1.4, 17.3, s=240, marker="*", facecolor="white", edgecolor=GRAY, lw=1.8, zorder=3)
    ax.annotate("transparent\n(0 edits, own binary;\nwalls on 3rd-party)", (1.4, 17.3),
                textcoords="offset points", xytext=(40, -2), fontsize=8.5, color="#5b6577",
                ha="left", va="center")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlim(1, 200); ax.set_ylim(2.5, 350)
    ax.set_xlabel("lines added to the application  (0–112)")
    ax.set_ylabel("speedup over synchronous offload  (×)")
    ax.grid(True, which="both", ls=":", lw=0.6, color="#cfd5e0", zorder=0)
    ax.set_xticks([1, 10, 100]); ax.set_xticklabels(["~0", "10", "100"])
    ax.set_yticks([3,10,30,100,300]); ax.set_yticklabels(["3","10","30","100","300"])
    # regime legend
    handles = [mpatches.Patch(color=SLATE, label="single event loop"),
               mpatches.Patch(color=TEAL, label="event loop + pool"),
               mpatches.Patch(color=GREEN, label="thread / goroutine pool"),
               mpatches.Patch(color=AMBER, label="process-per-connection (intra-query)"),
               mpatches.Patch(color=PURPLE, label="proxy (offload agent)")]
    ax.legend(handles=handles, fontsize=8.3, loc="lower left", bbox_to_anchor=(0.0, 0.0),
              frameon=True, framealpha=0.96)
    save(fig, "fig_spectrum")


# =====================================================================
# Fig 3 — a synchronous offload throttles the whole event loop
# =====================================================================
def fig_eventloop():
    apps = ["Redis", "Node.js", "Python"]
    sync = [971, 906, 921]
    asyn = [30675, 22440, 12745]
    fig, ax = plt.subplots(figsize=(6.4, 3.3))
    x = np.arange(len(apps)); w = 0.36
    b1 = ax.bar(x - w/2, sync, w, color=RED, ec=INK, lw=1.0, label="synchronous (blocks the loop)")
    b2 = ax.bar(x + w/2, asyn, w, color=SLATE, ec=INK, lw=1.0, label="async (few-line overlap)")
    ax.set_yscale("log")
    ax.set_ylim(300, 200000)
    ax.set_xticks(x); ax.set_xticklabels(apps, fontsize=11)
    ax.set_ylabel("requests / second  (log)")
    for xi, s, a in zip(x, sync, asyn):
        ax.text(xi + w/2, a*1.18, f"{a/s:.0f}×", ha="center", fontsize=11, fontweight="bold", color=SLATE)
    ax.legend(fontsize=9.5, loc="upper left", frameon=True)
    ax.grid(True, axis="y", ls=":", lw=0.6, color="#cfd5e0")
    ax.set_title("Single event loop, 1 ms offload: a few lines recover 14–31×", loc="left", fontsize=11)
    save(fig, "fig_eventloop")


# =====================================================================
# Fig 4 — concurrency-model regimes and the predicted win (schematic)
# =====================================================================
def fig_regimes():
    fig, ax = plt.subplots(figsize=(6.5, 3.5))
    ax.set_xlim(0, 10); ax.set_ylim(0, 10); ax.axis("off")
    cols = [
        ("single\nevent loop", SLATE, "sync offload\nstalls EVERYTHING", "dramatic\n5–31×", "Redis, Node"),
        ("event loop\n+ pool", TEAL, "stalls one\nloop of N", "large\n3.8–5×", "nginx, memcached"),
        ("thread /\ngoroutine pool", GREEN, "pool overlaps\nthe rest", "automatic\n59× (0 code)", "Apache, Go"),
        ("proxy +\nagent", PURPLE, "agent offloads\nasync", "native\n13×", "HAProxy, Envoy"),
        ("process-per-\nconnection", AMBER, "OS overlaps\nconnections free", "intra-query\n141–212×", "Postgres, MariaDB"),
    ]
    n = len(cols); w = 1.78; gap = 0.16; x0 = 0.15
    for i,(title,col,what,win,ex) in enumerate(cols):
        x = x0 + i*(w+gap)
        box(ax, x, 6.7, w, 2.7, fc=col, ec=INK, text="", round=0.08)
        ax.text(x+w/2, 8.7, title, ha="center", va="center", color="white", fontsize=9.5, fontweight="bold")
        ax.text(x+w/2, 7.5, what, ha="center", va="center", color="white", fontsize=7.6)
        box(ax, x, 4.6, w, 1.8, fc="white", ec=col, text="", round=0.08, lw=1.6)
        ax.text(x+w/2, 5.5, win, ha="center", va="center", color=INK, fontsize=9, fontweight="bold")
        ax.text(x+w/2, 3.9, ex, ha="center", va="center", color="#5b6577", fontsize=7.8, style="italic")
    ax.text(5.0, 9.85, "concurrency model  →  predicted minimal-edit win", ha="center",
            fontsize=11, fontweight="bold", color=INK)
    ax.text(5.0, 2.9, "Win grows with offload weight; it pays only when the offload outweighs per-request CPU.",
            ha="center", fontsize=8.6, color="#5b6577", style="italic")
    save(fig, "fig_regimes")


# =====================================================================
# Fig 5 — transparent M:N fiber runtime (schematic)
# =====================================================================
def fig_runtime():
    fig, ax = plt.subplots(figsize=(6.4, 3.4))
    ax.set_xlim(0, 10); ax.set_ylim(0, 10); ax.axis("off")
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
    # LD_PRELOAD note (with breathing room below the carrier box)
    ax.set_ylim(-0.7, 10)
    ax.text(5.0, 0.1, r"$\bf{LD\_PRELOAD}$ interposes pthread_create$\to$fiber, read/write/poll$\to$yield, offload$\to$yield."
            "\nThe application binary is unchanged.", ha="center", fontsize=8.6, color="#5b6577")
    save(fig, "fig_runtime")


# =====================================================================
# Fig 6 — three walls where transparency stops (schematic)
# =====================================================================
def fig_walls():
    fig, ax = plt.subplots(figsize=(6.5, 3.3))
    ax.set_xlim(0, 12); ax.set_ylim(0, 9); ax.axis("off")
    # three layers (kept clean)
    box(ax, 0.4, 6.6, 11.2, 1.7, fc=LIGHT, ec=INK, text="", round=0.03)
    ax.text(2.9, 7.45, "Application / runtime", ha="center", va="center", fontsize=10, color=INK, fontweight="bold")
    box(ax, 0.4, 4.0, 11.2, 1.7, fc="#DCE6F2", ec=SLATE, text="libc symbols  —  the interposition layer",
        fs=9.5, round=0.03, lw=1.8)
    box(ax, 0.4, 1.4, 11.2, 1.7, fc=LIGHT, ec=INK, text="Kernel / hardware", fs=10, round=0.03)
    ax.text(6.0, 8.6, "Where transparent interposition reaches — and the three walls", ha="center",
            fontsize=10.5, fontweight="bold", color=INK)
    # wall 1: raw futex bypasses libc (arrow from app, around libc, to kernel)
    arrow(ax, (1.5, 6.6), (1.5, 3.1), color=RED, lw=2.2, rad=0.0)
    ax.text(1.5, 0.7, "raw syscall(futex)\nbelow libc\n(InnoDB)", ha="center", fontsize=8, color=RED, fontweight="bold")
    # wall 2: native TLS register (lives in the app/runtime; not virtualizable)
    box(ax, 6.0, 6.75, 1.7, 1.4, fc="white", ec=RED, text="%fs\nTLS", fs=8.5, tc=RED, lw=1.8, round=0.06)
    ax.text(6.85, 0.7, "thread identity in\nnative TLS register\n(JVM, Go)", ha="center", fontsize=8, color=RED, fontweight="bold")
    # wall 3: no idle cpu
    box(ax, 9.4, 6.75, 1.7, 1.4, fc="white", ec=RED, text="CPU\n100%", fs=8.5, tc=RED, lw=1.8, round=0.06)
    ax.text(10.25, 0.7, "per-request CPU\n> offload\n(TLS crypto)", ha="center", fontsize=8, color=RED, fontweight="bold")
    save(fig, "fig_walls")


# =====================================================================
# Fig 7 — overlap pays only when the offload outweighs per-request work
# =====================================================================
def fig_weight():
    fig, ax = plt.subplots(figsize=(6.4, 3.3))
    Lx = np.array([38, 1000.0])
    node = np.array([1.18, 24.8]); py = np.array([1.0, 13.8])
    ax.plot(Lx, node, "-o", color=SLATE, lw=2.2, ms=8, label="Node.js")
    ax.plot(Lx, py, "-s", color=TEAL, lw=2.2, ms=7, label="Python")
    ax.axvspan(50, 78, color="#f3d9d3", alpha=0.7, zorder=0)
    ax.annotate("per-request\nCPU work", xy=(63, 1.5), xytext=(150, 4.6),
                ha="center", fontsize=8.4, color=RED,
                arrowprops=dict(arrowstyle="-", color=RED, lw=0.9))
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlim(25, 1500); ax.set_ylim(0.8, 40)
    ax.set_xlabel("offload latency  (µs, log)")
    ax.set_ylabel("speedup  (×, log)")
    ax.axhline(1.0, color="#999", ls="--", lw=1.0)
    ax.text(30, 1.02, "no benefit", fontsize=8, color="#777", va="bottom")
    ax.set_xticks([38,100,1000]); ax.set_xticklabels(["38\n(GPU AES)","100","1000\n(HSM/PQC)"])
    ax.set_yticks([1,3,10,30]); ax.set_yticklabels(["1","3","10","30"])
    ax.legend(fontsize=10, loc="upper left", frameon=True)
    ax.grid(True, which="both", ls=":", lw=0.6, color="#cfd5e0")
    ax.set_title("Overlap pays only when the offload outweighs per-request work", loc="left", fontsize=10.5)
    save(fig, "fig_weight")


# =====================================================================
# Fig 8 — correctness: overlap can corrupt shared state; the detector fixes it
# =====================================================================
def fig_correctness():
    fig, axes = plt.subplots(1, 2, figsize=(6.6, 3.0), gridspec_kw=dict(wspace=0.45, width_ratios=[1,1]))
    # left: lost updates
    ax = axes[0]
    labels = ["unlocked\n(overlap)", "detector\n+ enforce"]
    lost = [289411, 0]
    bars = ax.bar(labels, [max(v,1) for v in lost], color=[RED, GREEN], ec=INK, lw=1.0, width=0.6)
    ax.set_yscale("log"); ax.set_ylim(0.5, 1e6)
    ax.set_ylabel("lost updates  (log)")
    ax.text(0, 289411*1.4, "289 K\ncorrupted", ha="center", fontsize=9.5, color=RED, fontweight="bold")
    ax.text(1, 1.6, "0\ncorrect", ha="center", fontsize=9.5, color=GREEN, fontweight="bold")
    ax.set_title("Shared-state safety", fontsize=10.5)
    ax.grid(True, axis="y", ls=":", lw=0.5, color="#cfd5e0")
    # right: detector keeps overlap vs lock serializes
    ax = axes[1]
    cont = ["low", "high"]
    detector = [1.0, 1.0]; lock = [0.92, 1/30.]
    x = np.arange(2); w=0.36
    ax.bar(x-w/2, detector, w, color=SLATE, ec=INK, lw=1.0, label="detector (overlapped)")
    ax.bar(x+w/2, lock, w, color=AMBER, ec=INK, lw=1.0, label="lock (serialized)")
    ax.set_yscale("log"); ax.set_ylim(0.02, 1.8)
    ax.set_xticks(x); ax.set_xticklabels(["low\ncontention","high\ncontention"])
    ax.set_ylabel("relative throughput")
    ax.text(1, 1.05, "4–67×", ha="center", fontsize=10, color=SLATE, fontweight="bold")
    ax.legend(fontsize=8, loc="lower left", frameon=True)
    ax.set_title("Detector keeps offloads overlapped", fontsize=10.5)
    ax.grid(True, axis="y", ls=":", lw=0.5, color="#cfd5e0")
    save(fig, "fig_correctness")


# =====================================================================
# Fig 9 — intra-query offload pipelining in a database (gantt)
# =====================================================================
def fig_dbpipeline():
    fig, axes = plt.subplots(2, 1, figsize=(6.4, 3.0), gridspec_kw=dict(hspace=0.7))
    N = 16  # draw 16 to represent 256
    # serial: staircase
    ax = axes[0]
    for i in range(N):
        ax.add_patch(Rectangle((i, 0.2), 0.92, 0.6, fc=AMBER, ec=INK, lw=0.5))
    ax.set_xlim(0, N+2.5); ax.set_ylim(0, 1.1)
    ax.text(N+0.4, 0.5, "254 ms", va="center", fontsize=11, fontweight="bold", color=RED)
    ax.set_title("serial  accel_sync(256):  one offload after another", loc="left", fontsize=10.5)
    # pipelined: all in flight at once (a vertical stack of short, parallel bars)
    ax = axes[1]
    M = 12
    for i in range(M):
        ax.add_patch(Rectangle((0.2, i*0.085+0.08), 1.5, 0.055, fc=SLATE, ec="white", lw=0.6))
    ax.add_patch(FancyArrowPatch((0.1, 0.06), (0.1, M*0.085+0.06), arrowstyle="<->",
                 mutation_scale=8, lw=1.3, color=RED))
    ax.set_xlim(0, N+2.5); ax.set_ylim(0, 1.15)
    ax.text(1.95, M*0.085/2+0.06, "1.8 ms", va="center", ha="left", fontsize=11, fontweight="bold", color=SLATE)
    ax.text(9.2, M*0.085/2+0.06, "= 141× faster  (all 256 offloads in flight at once)", va="center",
            fontsize=10, color=INK, fontweight="bold")
    ax.set_title("pipelined  accel_async(256):  all offloads in flight", loc="left", fontsize=10.5)
    for ax in axes:
        ax.set_yticks([]); ax.set_xticks([])
        for s in ax.spines.values(): s.set_visible(False)
    save(fig, "fig_dbpipeline")


# =====================================================================
# Fig 10 — HAProxy with a real offload agent (SPOE)
# =====================================================================
def fig_proxy():
    fig, ax = plt.subplots(figsize=(6.3, 2.7))
    labels = ["serial agent\n(1 worker)", "overlapping agent\n(64 workers)"]
    rps = [499, 6644]
    bars = ax.barh(labels, rps, color=[RED, PURPLE], ec=INK, lw=1.0, height=0.55)
    ax.set_xlim(0, 7800)
    for y,(v) in enumerate(rps):
        ax.text(v+150, y, f"{v:,} rps", va="center", fontsize=11, fontweight="bold",
                color=RED if y==0 else PURPLE)
    ax.text(6644*0.5, 1.32, "13.3×", ha="center", fontsize=12, fontweight="bold", color=PURPLE)
    ax.set_xlabel("requests / second")
    ax.set_title("HAProxy + SPOE: a real offload agent overlaps while the proxy keeps serving",
                 loc="left", fontsize=10)
    ax.grid(True, axis="x", ls=":", lw=0.5, color="#cfd5e0")
    save(fig, "fig_proxy")


# =====================================================================
# Fig 11 — a classifier: futex density predicts fiberizability
# =====================================================================
def fig_classifier():
    fig, ax = plt.subplots(figsize=(6.4, 3.0))
    apps = ["Redis", "stunnel", "memcached", "MariaDB\n(InnoDB)"]
    futex = [25, 24, 1258, 12966]
    colors = [TEAL, TEAL, TEAL, RED]
    bars = ax.bar(apps, futex, color=colors, ec=INK, lw=1.0, width=0.6)
    ax.set_yscale("log"); ax.set_ylim(8, 40000)
    ax.set_ylabel("per-request futex calls  (log)")
    ax.text(3, 12966*1.4, "+ io_uring\n→ the wall", ha="center", fontsize=9, color=RED, fontweight="bold")
    ax.axhline(100, color="#888", ls="--", lw=1.0)
    ax.text(0.0, 130, "fiberizable", fontsize=9, color=TEAL, va="bottom", fontweight="bold")
    ax.text(3.0, 130, "sub-libc wall", fontsize=9, color=RED, va="bottom", ha="center", fontweight="bold")
    ax.grid(True, axis="y", ls=":", lw=0.5, color="#cfd5e0")
    ax.set_title("Futex density predicts whether a binary can be fiberized", loc="left", fontsize=10.5)
    save(fig, "fig_classifier")


# =====================================================================
# Fig 12 — the minimal-edit recipe (schematic)
# =====================================================================
def fig_recipe():
    fig, ax = plt.subplots(figsize=(6.5, 3.1))
    ax.set_xlim(0, 12); ax.set_ylim(0, 7); ax.axis("off")
    # the handler reaches the offload
    box(ax, 0.3, 4.6, 2.5, 1.6, fc=SLATE, ec=INK, text="handler reaches\nthe offload", fs=9.5, tc="white", round=0.07)
    # step 1: submit
    box(ax, 4.2, 4.9, 3.0, 1.5, fc="white", ec=GREEN, text="1. submit to a\nbackground executor", fs=9, lw=1.6, round=0.07)
    arrow(ax, (2.8, 5.4), (4.2, 5.6), color=GREEN)
    # device
    box(ax, 8.6, 4.9, 3.0, 1.5, fc=AMBER, ec=INK, text="accelerator runs\nthe offload", fs=9, tc="white", round=0.07)
    arrow(ax, (7.2, 5.6), (8.6, 5.6), color=AMBER)
    # step 2: suspend
    box(ax, 4.2, 2.6, 3.0, 1.5, fc="white", ec=PURPLE, text="2. suspend the request\n(server's own primitive)", fs=9, lw=1.6, round=0.07)
    arrow(ax, (1.55, 4.6), (4.4, 4.1), color=PURPLE, rad=-0.2)
    # event loop stays free
    box(ax, 0.3, 2.5, 2.5, 1.5, fc=LIGHT, ec=TEAL, text="loop serves\nother requests", fs=9, lw=1.5, round=0.07)
    arrow(ax, (2.8, 3.2), (4.2, 3.3), color=TEAL, ls="--")
    # step 3: resume
    box(ax, 8.6, 2.6, 3.0, 1.5, fc="white", ec=SLATE, text="3. resume + reply\non completion", fs=9, lw=1.6, round=0.07)
    arrow(ax, (10.1, 4.9), (10.1, 4.1), color=SLATE)
    arrow(ax, (8.6, 3.3), (7.2, 3.3), color=SLATE, ls="-")
    ax.text(6.0, 6.7, "The minimal-edit recipe: route the offload through the server's existing suspend/resume machinery",
            ha="center", fontsize=10, fontweight="bold", color=INK)
    ax.text(6.0, 1.2, "18–112 lines added, 0–1 modified — the machinery already exists; one only reroutes the offload.",
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
    axes[0].legend(fontsize=8.3, loc="upper left", frameon=True)
    axes[0].annotate("", xy=(410, 42), xytext=(103, 42),
                     arrowprops=dict(arrowstyle="<->", color=INK))
    axes[0].text(256, 27, "~4× the load at\nthe same latency", ha="center",
                 fontsize=8.0, color=INK)
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
    ax.text(3.4, 8.7, "This paper:\ntransparent $\\to$ minimal-edit\n(0–112 lines, 10 server types)",
            ha="center", fontsize=9.5, color=SLATE, fontweight="bold", zorder=3)
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


if __name__ == "__main__":
    fig_pipeline(); fig_spectrum(); fig_eventloop(); fig_regimes(); fig_runtime()
    fig_walls(); fig_weight(); fig_correctness(); fig_dbpipeline(); fig_proxy(); fig_classifier()
    fig_recipe(); fig_condsweep(); fig_landscape(); fig_latency(); fig_positioning()
    print("ALL FIGURES DONE")
