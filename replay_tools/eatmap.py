import sys, collections; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from sim import Game
g=Game(sys.argv[1]); r0=int(sys.argv[2]); r1=int(sys.argv[3])
team=dict(g.team); rnd=0; cur=None
eat={'A':collections.Counter(),'B':collections.Counter()}; spawn=collections.Counter()
for e in g.rp.events:
    k=e[0]
    if k=='round': rnd=e[1]
    elif k=='turn': cur=e[1]
    elif k=='split': team[e[2]]=e[3]
    elif k=='tile':
        if r0<=rnd<r1:
            if e[3]: spawn[(e[1],e[2])]+=1
            elif cur is not None: eat[team.get(cur,'?')][(e[1],e[2])]+=1
W,H=g.W,g.H
def edge(x,y,d): return g.edge(x,y,d)[0]
print(g.name, 'rounds',r0,r1,'eatA',sum(eat['A'].values()),'eatB',sum(eat['B'].values()))
for y in range(H):
    row=''
    for x in range(W):
        a=eat['A'][(x,y)]; b=eat['B'][(x,y)]
        c='.' if not spawn[(x,y)] and not a and not b else ('A' if a>b else 'B' if b>a else ('=' if a else 'o'))
        wl='|' if edge(x,y,3)==1 else ('P' if edge(x,y,3)==2 else ' ')
        row+=wl+c
    print('%2d'%y,row)
