#!/usr/bin/env python3
"""instrument.py FILE: add the v5.3 audit's DIAG trace points to a copy of main.cpp (in place).

The points are inserted after anchor strings, so this keeps working while main.cpp changes elsewhere. DIAG compiles to
nothing unless BOT_DIAG is defined (mkvariant.sh ... diag), so these lines never reach a submitted build.
"""
import sys

POINTS = [
    # F_CYCLE2 / F_CYCLE: a loop breakout starts
    ("        s.cycle_break_until = round + 10;\n",
     '        DIAG("a53loop " << c.get_id() << \' \' << round << " span " << span << " at " << here.x << \',\' << here.y);\n'),
    # F_ALPHA_PORTAL: the alpha sets off for a portal, splits its 2-long head off, or hands the role over
    ("                    port = ap;\n                    alpha_capture = true;\n",
     '                    DIAG("a53cap " << c.get_id() << \' \' << round << " pid " << ap->id << " cost " << ap->cost << " len "\n'
     '                                   << c.get_length());\n'),
    ("        if (alpha_capture && long_body() && port->approach == here && can_split_safe(c.get_length() - 2)) {\n",
     '            DIAG("a53capsplit " << c.get_id() << \' \' << round << " pid " << port->id << " len " << c.get_length());\n'),
    ("            s.capture_until = round + 6;\n",
     '            DIAG("a53capho " << c.get_id() << \' \' << round << " pid " << port->id << " to " << best_id << " tolen " << best_len\n'
     '                             << " len " << c.get_length());\n'),
    # F_LONG_PORTAL: forced across a portal, a long dragon splits and sends only its 2-long head
    ("            s.capture_portal = edge - 1;\n",
     '            DIAG("a53sacrifice " << c.get_id() << \' \' << round << " pid " << edge - 1 << " len " << c.get_length());\n'),
    # F_HANDOVER: a 4+ long split child decides whether it is the alpha
    ("            if (s.alpha) s.mantle_round = round;\n",
     '            DIAG("a53hochild " << c.get_id() << \' \' << round << " len " << c.get_length() << " alpha " << s.alpha);\n'),
    # F_HANDOVER / F_ALPHA_PORTAL: a handover packet arrives
    ("            int target = px | (py << 6);\n",
     '            DIAG("a53horecv " << c.get_id() << \' \' << round << " to " << target << " old " << bits << " age " << age);\n'),
    # F_HAZARD: a trapped dragon announces its pocket's mouth
    ("                add_hazard(s.enc_mouth, s.enc_dir, round);\n",
     '                DIAG("a53hz " << c.get_id() << \' \' << round << " at " << s.enc_mouth.x << \',\' << s.enc_mouth.y << " dir "\n'
     '                              << s.enc_dir << " len " << c.get_length());\n'),
    # F_HAZARD(_RR): a hazard we did not know yet arrives on sonar
    ("            if (!F_HAZARD || age > HAZARD_TTL || px >= w || py >= h || !(bits & 4)) return;\n",
     '#ifdef BOT_DIAG\n'
     '            { bool known = false; for (const auto &hz : s.hazards) if (hz.p == Position{px, py} && hz.dir == (bits & 3) && hz.origin > -1000) known = true;\n'
     '              if (!known) DIAG("a53hzrecv " << c.get_id() << \' \' << round << " at " << px << \',\' << py << " dir " << (bits & 3)\n'
     '                               << " origin " << origin); }\n'
     '#endif\n'),
    # F_PORTAL_FIX rule 3: a free portal a few steps away preempts a remembered / upcoming pearl
    ("                                   (F_PORTAL_FIX && !pearls && !future && port->cost <= 3));\n",
     '        if (use_portal && !((!pearls && !future && !rem_pearl) || early_portal || alpha_capture))\n'
     '            DIAG("a53preempt " << c.get_id() << \' \' << round << " pid " << port->id << " cost " << port->cost);\n'),
    # F_PORTAL_FIX rule 1: a portal user that came out where nothing spawns is released from residency
    ("                pp.shun_until = std::max(pp.shun_until, round + 20);\n",
     '                DIAG("a53release " << c.get_id() << \' \' << round << " pid " << s.pending_portal);\n'),
]

path = sys.argv[1]
src = open(path).read()
for anchor, add in POINTS:
    n = src.count(anchor)
    if n != 1:
        sys.exit(f"anchor found {n} times: {anchor!r}")
    src = src.replace(anchor, anchor + add)
open(path, "w").write(src)
