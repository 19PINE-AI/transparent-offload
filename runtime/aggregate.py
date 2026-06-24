#!/usr/bin/env python3
# Aggregate repeated-trial CSVs: mean, std, 95% CI (normal approx), and error-bar plots.
import csv, sys, math, collections
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt

def ci95(xs):
    n=len(xs); m=sum(xs)/n
    if n<2: return m,0.0
    sd=math.sqrt(sum((x-m)**2 for x in xs)/(n-1))
    return m, 1.96*sd/math.sqrt(n)

def load(path, key_cols, val_col):
    g=collections.defaultdict(list)
    for r in csv.DictReader(open(path)):
        key=tuple(r[k] for k in key_cols)
        g[key].append(float(r[val_col]))
    return g

if __name__=='__main__' and sys.argv[1]=='sweep_L':
    g=load('sweep_L_reps.csv',['mode','L_us'],'tput_reqps')
    sty={'busy':('o-','#d62728','busy-wait (approach 2)'),
         'block':('s-','#1f77b4','block + ctx-switch (approach 1)'),
         'coro':('^-','#2ca02c','transparent coroutine (ours)')}
    fig,ax=plt.subplots(figsize=(6.2,4.2))
    print("mode,L_us,mean_tput,ci95,n")
    for m in ['busy','block','coro']:
        Ls=sorted({float(k[1]) for k in g if k[0]==m})
        xs,ys,es=[],[],[]
        for L in Ls:
            vals=g[(m,str(L))]; mean,ci=ci95(vals)
            xs.append(L); ys.append(mean/1e3); es.append(ci/1e3)
            print(f"{m},{L},{mean:.0f},{ci:.0f},{len(vals)}")
        ax.errorbar(xs,ys,yerr=es,fmt=sty[m][0],color=sty[m][1],label=sty[m][2],lw=2,ms=6,capsize=3)
    ax.set_xscale('log'); ax.set_xlabel('offload latency L (µs)')
    ax.set_ylabel('throughput (K req/s, 1 server core)')
    ax.set_title('Hiding accelerator latency (mean ± 95% CI, n=8)\nW=2µs CPU/req, C=128')
    ax.legend(); ax.grid(True,which='both',alpha=0.3); fig.tight_layout(); fig.savefig('fig_sweep_L_ci.png',dpi=130)
    print("wrote fig_sweep_L_ci.png")

elif __name__=='__main__' and sys.argv[1]=='wsweep':
    g=load('sweep_W_reps.csv',['mode','W_us'],'tput_reqps')
    sty={'busy':('o-','#d62728','busy-wait'),'block':('s-','#1f77b4','block+ctxsw'),'coro':('^-','#2ca02c','coroutine (ours)')}
    fig,ax=plt.subplots(figsize=(6.2,4.2))
    print("mode,W_us,mean_tput,ci95,n")
    for m in ['busy','block','coro']:
        Ws=sorted({float(k[1]) for k in g if k[0]==m})
        xs,ys,es=[],[],[]
        for W in Ws:
            vals=g[(m,str(W))]; mean,ci=ci95(vals); xs.append(W); ys.append(mean/1e3); es.append(ci/1e3)
            print(f"{m},{W},{mean:.0f},{ci:.0f},{len(vals)}")
        ax.errorbar(xs,ys,yerr=es,fmt=sty[m][0],color=sty[m][1],label=sty[m][2],lw=2,ms=6,capsize=3)
    ax.set_xlabel('CPU work per request W (µs)'); ax.set_ylabel('throughput (K req/s)')
    ax.set_title('Sensitivity to CPU work (L=20µs, C=128, mean ± 95% CI)')
    ax.legend(); ax.grid(True,alpha=0.3); fig.tight_layout(); fig.savefig('fig_wsweep_ci.png',dpi=130)
    print("wrote fig_wsweep_ci.png")
