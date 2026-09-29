import sys, collections; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from sim import Game
for f in sys.argv[1:]:
    g=Game(f); team=dict(g.team); rnd=0; cur=None
    eat=collections.defaultdict(collections.Counter); spl=collections.defaultdict(collections.Counter)
    for e in g.rp.events:
        k=e[0]
        if k=='round': rnd=e[1]
        elif k=='turn': cur=e[1]
        elif k=='tile' and not e[3] and cur is not None: eat[rnd//50][team.get(cur,'?')]+=1
        elif k=='split': team[e[2]]=e[3]; spl[rnd//50][e[3]]+=1
    g=Game(f); peak={'A':0,'B':0}; units=collections.defaultdict(dict)
    for t,r,i in g.run():
        for tm in 'AB':
            n=sum(1 for j in g.bodies if g.team[j]==tm); peak[tm]=max(peak[tm],n)
            if r%50==0: units[r][tm]=n
    res=g.rp.result
    print(f"{g.name:18s} winner {res['winner']} rounds {max(units) if units else 0}+ peak units A {peak['A']} B {peak['B']}")
    for b in sorted(eat):
        u=units.get(b*50,{})
        print(f"   r{b*50:3d}-{b*50+49:3d}: eaten A {eat[b]['A']:4d} B {eat[b]['B']:4d} | splits A {spl[b]['A']:3d} B {spl[b]['B']:3d} | units at start A {u.get('A','-')} B {u.get('B','-')}")
