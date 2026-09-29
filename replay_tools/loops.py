"""loops.py FILE [team] [window] [maxdistinct]: dragons whose head visits <= maxdistinct tiles over `window` consecutive own turns, no growth"""
import sys, collections; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from sim import Game
f=sys.argv[1]; team=sys.argv[2] if len(sys.argv)>2 else 'A'; W=int(sys.argv[3]) if len(sys.argv)>3 else 16; M=int(sys.argv[4]) if len(sys.argv)>4 else 8
g=Game(f); hist=collections.defaultdict(list); rep={}
for t,r,i in g.run():
    b=g.bodies.get(i)
    if b is None or g.team.get(i)!=team: continue
    hist[i].append((t,r,b[0],len(b),g.ind.get(i,'')))
    h=hist[i][-W:]
    if len(h)==W and len({x[2] for x in h})<=M and h[0][3]>=h[-1][3]:
        if i not in rep or rep[i][-1][1] < r-W:
            rep.setdefault(i,[]).append((t,r,sorted({x[2] for x in h}),h[-1][3],h[-1][4]))
        else:
            rep[i][-1]=(rep[i][-1][0],rep[i][-1][1],rep[i][-1][2],rep[i][-1][3],rep[i][-1][4]); 
tot=0
for i,v in rep.items():
    for x in v:
        tot+=1
        print('id',i,'turn',x[0],'round',x[1],'len',x[3],x[4],'tiles',x[2][:10])
print('loops',tot)
