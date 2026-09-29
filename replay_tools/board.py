"""show.py FILE --turn T | --round R [--id I] [--box x0 x1 y0 y1] [--after]: board before the given turn (sim counter) / at start of round R"""
import sys, argparse; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from sim import Game
ap=argparse.ArgumentParser(); ap.add_argument('file'); ap.add_argument('--turn',type=int); ap.add_argument('--round',type=int)
ap.add_argument('--id',type=int); ap.add_argument('--box',type=int,nargs=4); ap.add_argument('--rounds',type=int,default=1)
a=ap.parse_args()
g=Game(a.file)
def dump(t,r,cur):
    print(f'--- turn {t} round {r} next-to-move {cur}')
    if a.box:
        x0,x1,y0,y1=a.box
    else:
        b=g.bodies.get(a.id if a.id is not None else cur)
        h=b[0] if b else (0,0)
        x0,x1,y0,y1=h[0]-6,h[0]+6,h[1]-6,h[1]+6
    print(g.render(x0,x1,y0,y1))
    for (i,tm,L,hd,ind) in sorted(g.heads(x0,x1,y0,y1)):
        print(f'  id {i} {tm} len {L} head {hd} {ind}')
# run: yield after each dragon turn (state after dragon cur's turn has been applied? check sim: yields when next turn starts, before new turn's events)
prev=None; shown=0
it=g.run()
tcount=0
for t,r,cur in it:
    # at this yield, events of turn t (dragon cur) processed
    if a.turn is not None and t==a.turn-1: dump(t+1,r,'?'); shown+=1
    if a.round is not None and r>=a.round and (prev is None or prev<a.round or (shown and r<a.round+a.rounds and r!=prev)):
        if r< a.round+a.rounds: dump(t,r,cur); shown+=1
        else: break
    prev=r
    if a.turn is not None and t>a.turn: break
