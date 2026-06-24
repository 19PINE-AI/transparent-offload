#!/usr/bin/env python3
import csv, sys, math, collections
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
def ci95(xs):
    n=len(xs); m=sum(xs)/n
    if n<2: return m,0.0
    sd=math.sqrt(sum((x-m)**2 for x in xs)/(n-1)); return m,1.96*sd/math.sqrt(n)
def val(s):  # strip "k=v" -> float(v)
    return float(s.split('=')[1]) if '=' in s else float(s)

what=sys.argv[1]
if what=='blockthreads':
    g=collections.defaultdict(list)
    for r in csv.DictReader(open('block_threads_reps.csv')): g[int(r['C'])].append(float(r['tput_reqps']))
    Cs=sorted(g); print("C,mean_tput,ci95")
    xs,ys,es=[],[],[]
    for c in Cs:
        m,ci=ci95(g[c]); xs.append(c); ys.append(m/1e3); es.append(ci/1e3); print(f"{c},{m:.0f},{ci:.0f}")
    fig,ax=plt.subplots(figsize=(6,4))
    ax.errorbar(xs,ys,yerr=es,fmt='s-',color='#1f77b4',capsize=3,lw=2,label='block + ctx-switch')
    ax.axhline(453,color='#2ca02c',ls='--',label='coro (1 thread) ≈ 453K')
    ax.set_xscale('log',base=2); ax.set_xlabel('# OS threads (block mode)'); ax.set_ylabel('throughput (K req/s)')
    ax.set_title('block needs many threads to hide latency, and still tops out\n(L=20µs, W=2µs; coro matches with ONE thread)')
    ax.legend(); ax.grid(True,which='both',alpha=0.3); fig.tight_layout(); fig.savefig('fig_blockthreads.png',dpi=130)
    print("wrote fig_blockthreads.png")

elif what=='detector':
    # per (mode,NKEY): lost_updates, fallback_pct, tput
    agg=collections.defaultdict(lambda: collections.defaultdict(list))
    for r in csv.DictReader(open('detector_reps.csv')):
        k=(r['mode'], int(val(r['NKEY'])))
        agg[k]['lost'].append(val(r['lost_updates'])); agg[k]['fb'].append(val(r['fallback_pct'])); agg[k]['tput'].append(val(r['tput_ops_s']))
    print("mode,NKEY,lost_mean,fb_mean±ci,tput_mean±ci,n")
    for (mode,nk) in sorted(agg, key=lambda x:(x[0],x[1])):
        lm,_=ci95(agg[(mode,nk)]['lost']); fbm,fbc=ci95(agg[(mode,nk)]['fb']); tm,tc=ci95(agg[(mode,nk)]['tput'])
        print(f"{mode},{nk},{lm:.0f},{fbm:.2f}±{fbc:.2f},{tm:.0f}±{tc:.0f},{len(agg[(mode,nk)]['lost'])}")

elif what=='gpu':
    agg=collections.defaultdict(list)
    for r in csv.DictReader(open('gpu_reps.csv')): agg[r['mode']].append(val(r['tput_ops_s']))
    print("mode,mean_ops_s,ci95,n")
    res={}
    for m in agg: mn,ci=ci95(agg[m]); res[m]=mn; print(f"{m},{mn:.0f},{ci:.0f},{len(agg[m])}")
    if 'busy' in res and 'coro' in res: print(f"# coro/busy ratio = {res['coro']/res['busy']:.2f}x")
