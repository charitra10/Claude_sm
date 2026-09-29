"""trace.py FILE ID [t0 t1]: per turn of dragon ID: turn, round, len, head, action, indicator; splits/deaths involving it"""
import sys; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from sim import Game
g=Game(sys.argv[1]); I=int(sys.argv[2]); t0=int(sys.argv[3]) if len(sys.argv)>3 else 0; t1=int(sys.argv[4]) if len(sys.argv)>4 else 10**9
tc=-1; rnd=0; cur=None
evs=g.rp.events
# reuse Game.run but also inspect raw events: simpler: iterate run() and look at last action
acts={}
orig=g.rp.events
for t,r,cur in g.run():
    if cur==I and t0<=t<=t1:
        b=g.bodies.get(I)
        print(t, 'r',r, 'len',len(b) if b else None, 'head',b[0] if b else None,'tail',b[-1] if b else None, g.ind.get(I,''), 'dead' if I in g.dead else '')
