# SPDX-License-Identifier: GPL-3.0-only
"""OPX fork: the OPX factory bank (tools/opx_factory.py compiles it). One P() per patch, in SLOT order: F1..F12 in
every OPX firmware, F13.. in Felucca-OPX Solo only. Original sounds in the usual categories of opsix soundsets
(keys, plucks and mallets, basses, leads, brass and winds, pads and strings, drums, effects, chords and sequences);
none is a copy of a Korg program.

O(level, r=ratio, det=cents, a=, d=, rl= ms, s= %, c= curve, fb=, vel=, ...): an operator (opx_factory.O).
Every patch gives the MOD WHEEL something to do. Levels: a modulator's LEVEL L at full envelope swings the phase by
4 pi (L / 100)^3 rad (50: 1.6 rad, 60: 2.7, 70: 4.3, 80: 6.4). Algorithms used (carriers in brackets):
  1 [1 3] 2>1 6>5>4>3      5 [1 3 5] 2>1 4>3 6>5      7 [1 3] 2>1 4,5>3 6>5     16 [1] 2,3,5>1 4>3 6>5
  19 [1 4 5] 3>2>1 6>4,5   21 [1 2 4 5] 3>1,2 6>4,5   22 [1 3 4 5] 2>1 6>3,4,5   25 [1..5] 6>4,5
  31 [1..5] 6>5            32 [1..6]                  33 [1] 2>1 3,5,6>2 4>3
"""
from opx_factory import C, EG, FLT, FX, LFO, O, OFF, P, VP

R1 = 7          # (coarse index of ratio 1: the factory patches of 0.2.0 by their codes, unchanged)


def OPC(l, co, fi, dt, a, d, r, s, cv, fb, v):        # (an operator as eng_opx.c's OP macro had it)
    return O(l, (co, fi), det=dt, a=C(a), d=C(d), rl=C(r), s=s, c=cv, fb=fb, vel=v)


OFFC = OPC(0, R1, 1000, 0, 0, 0, 0, 0, 0, 0, 0)

# ================================================================== F1..F7: the factory patches of 0.2.0, unchanged ==
P("E.PIANO", "K", 5, [
    OPC(100, R1, 1000, 2, 1, 682, 300, 0, 8, 0, 60), OPC(62, R1, 1000, -2, 1, 620, 300, 0, 8, 0, 70),
    OPC(80, R1, 1000, -3, 1, 764, 400, 0, 8, 0, 60), OPC(58, R1 + 13, 1000, 3, 1, 450, 300, 0, 8, 0, 80),
    OPC(45, R1, 1000, 0, 1, 806, 400, 0, 8, 0, 60), OPC(55, R1 + 13, 1000, 0, 1, 414, 300, 0, 9, 40, 80)],
  vp=[VP("MW", "OP4.LEVEL", 25)], fx=[FX("PAN", 35, 1, 3.5, 50), FX("3EQ", 2, -1, 0, 1000)],
  sends=(0, 40, 20, 35), pat="KEYS", note="three pairs: a tine (op 3, 4), a bell (op 5, 6); MW the tine")
P("BELL", "U", 5, [
    OPC(100, R1, 1000, 0, 1, 806, 806, 0, 10, 0, 50), OPC(52, R1 + 2, 1410, 0, 1, 764, 764, 0, 10, 0, 60),
    OPC(85, R1, 1000, 4, 1, 764, 764, 0, 10, 0, 50), OPC(48, R1 + 5, 1000, 0, 1, 682, 682, 0, 10, 0, 60),
    OPC(70, R1 + 1, 1000, -4, 1, 720, 720, 0, 10, 0, 50), OPC(42, R1 + 7, 1414, 0, 1, 600, 600, 0, 10, 0, 60)],
  fx=[FX("3EQ", -3, 2, 0, 1000), FX("PAN", 20, 1, 0.3, 50)],
  sends=(0, 10, 30, 70), pat="BELL", note="inharmonic modulators")
P("FM BASS", "B", 1, [
    OPC(100, R1 - 1, 1000, 0, 1, 682, 207, 70, 6, 0, 40), OPC(78, R1 - 1, 1000, 0, 1, 476, 207, 30, 6, 0, 60),
    OPC(80, R1 - 1, 1000, 1, 1, 620, 207, 60, 6, 0, 40), OPC(70, R1, 1000, 0, 1, 414, 207, 20, 6, 0, 60),
    OPC(55, R1 + 2, 1000, 0, 1, 306, 207, 0, 6, 0, 60), OPC(50, R1, 1000, 0, 1, 351, 207, 0, 6, 60, 60)],
  eg=[None, EG(d=C(512), rl=C(414), s=0, c=4)],
  flt=FLT("LPF24", C(600), reso=20, eg2=36, eg2_c="VEL", kh=43, kc=48),
  fx=[FX("COMP", 100, 40, 30, -6)], sends=(0, 0, 10, 10), mono=1, pat="OFFBEAT", note="a pair and a stack")
P("BRASS", "L", 22, [
    OPC(100, R1, 1000, 0, 351, 620, 300, 85, 4, 0, 30), OPC(72, R1, 1000, 0, 394, 558, 300, 70, 4, 0, 50),
    OPC(90, R1, 1000, 6, 351, 620, 300, 85, 4, 0, 30), OPC(90, R1, 1000, -6, 351, 620, 300, 85, 4, 0, 30),
    OPC(70, R1 + 1, 1000, 0, 351, 620, 300, 80, 4, 0, 30), OPC(74, R1, 1000, 0, 414, 600, 300, 65, 4, 45, 50)],
  eg=[None, EG(a=C(394), d=C(574), rl=C(512), s=50, c=4)], lfo=[LFO("SIN", C(701), ks=2, fade=600)], plfo=0.12,
  flt=FLT("LPF12", C(651), reso=10, eg2=30, eg2_c="VEL", kh=64, kc=60),
  fx=[FX("3EQ", 0, -2, 3, 1500)], sends=(0, 25, 20, 40), pat="LEAD", note="one modulator on three carriers")
P("SAW LEAD", "L", 32, [
    OPC(100, R1, 1000, 0, 1, 682, 207, 90, 3, 75, 20), OPC(90, R1, 1000, 7, 1, 682, 207, 90, 3, 75, 20),
    OPC(90, R1, 1000, -7, 1, 682, 207, 90, 3, 75, 20), OFFC, OFFC, OFFC],
  eg=[None, EG(d=C(538), rl=C(476), s=30, c=6)], lfo=[LFO("SIN", C(701), ks=1)], plfo=0.5, plfo_c="MW",
  flt=FLT("LPF24", C(765), reso=30, eg2=18, kh=64, kc=60),
  fx=[FX("DIST", 25, -8, 2500, 2)], sends=(10, 30, 30, 30), mono=1, pat="LEAD",
  note="three detuned carriers at the sawtooth-like feedback")
P("LOOP PAD", "P", 34, [
    OPC(100, R1, 1000, 0, 600, 826, 682, 90, 5, 0, 20), OPC(55, R1, 1000, 3, 620, 826, 682, 80, 5, 0, 30),
    OPC(50, R1 + 1, 1000, 0, 656, 826, 682, 70, 5, 0, 30), OPC(48, R1, 1000, -3, 682, 826, 682, 70, 5, 0, 30),
    OPC(45, R1 + 2, 1000, 0, 682, 826, 682, 60, 5, 0, 30), OPC(45, R1, 1000, 0, 718, 826, 682, 60, 5, 0, 30)],
  fb=70, eg=[None, EG(a=C(656), d=C(718), rl=C(656), s=60, c=5)], lfo=[None, LFO("TRI", C(333), ks=1)],
  flt=FLT("LPF12", C(715), reso=15, eg2=12, kh=0, kc=60, lfo2=8),
  fx=[FX("PHA", 50, 70, 0.15, 40), FX("PAN", 30, 1, 0.1, 50)], sends=(0, 60, 30, 70), pat="PAD",
  note="the 6-operator chain with its 4 -> 6 loop (ALG FB)")
P("X-MOD", "F", "USER", [
    OPC(100, R1, 1000, 0, 1, 764, 414, 50, 6, 0, 40), OPC(90, R1 + 1, 1005, 0, 1, 764, 414, 50, 6, 0, 40),
    OPC(50, R1 + 2, 1000, 0, 1, 558, 414, 10, 6, 0, 60), OFFC, OFFC, OFFC],
  ua=[[0, 12, 0, 0, 0, 0], [10, 0, 0, 0, 0, 0], [35, 35, 0, 0, 0, 0], [0] * 6, [0] * 6, [0] * 6], out="1 2",
  lfo=[None, None, LFO("TRI", C(378), ks=2)],
  vp=[VP("MW", "OP3.LEVEL", 30), VP("LFO3", "OP2.PITCH", 0.1)],
  fx=[FX("FLTR", 65, 40, 35, 0.5), FX("DEC", 25, 16000, 12.0, 0)], sends=(0, 20, 30, 40), pat="MELODY",
  note="USER: two carriers that modulate each other, a third modulating both")

# ======================================================================= F8..F12: in both firmwares, one per kind ==
P("DRAWBARS", "K", 32, [
    O(80, 0.5, a=3, rl=40), O(85, 1, a=3, rl=40), O(55, 1.5, a=3, rl=40), O(70, 2, a=3, rl=40),
    O(62, 3, a=1, d=450, s=0, rl=40, c=9, vel=40), O(55, 4, a=3, rl=40)],
  lfo=[LFO("SIN", 6.8, ks=0)], plfo=0.12, plfo_c="MW",
  fx=[FX("3EQ", 2, 1, -1, 800), FX("PAN", 25, "SINE", 0.8, 50)], sends=(0, 50, 0, 30), pat="KEYS",
  gain=-0.6, note="16' 8' 5 1/3' 4' 2' and a decaying 2 2/3' (percussion); MW a scanner-like vibrato")
P("PLUCKER", "U", 5, [
    O(92, 1, a=0, d=900, s=0, rl=250, c=8, vel=40), O(72, 1, d=180, s=0, rl=200, c=8, vel=70),
    O(70, 1, det=6, d=1100, s=0, rl=250, c=8, vel=40), O(55, 3, d=120, s=0, rl=200, c=8, vel=80),
    O(50, 2, det=-5, d=600, s=0, rl=250, c=8), O(42, 1, d=300, s=0, rl=200, c=8, fb=40)],
  eg=[None, EG(d=350, s=0, rl=300, c=6)], flt=FLT("LPF12", 1800, reso=15, eg2=30, eg2_c="VEL", kh=48),
  vp=[VP("MW", "FLT.CUTOFF", 18)], fx=[FX("3EQ", 0, 2, 0, 1000)], sends=(0, 30, 45, 30), pat="MELODY",
  gain=2.4, note="three decaying pairs, the filter's pluck by velocity; MW opens it")
P("STRINGS", "P", 32, [
    O(75, 1, det=-9, wave="SAW", a=450, rl=900), O(75, 1, det=8, wave="SAW", a=480, rl=900),
    O(62, 1, det=-2, wave="SAW", a=500, rl=900), O(55, 0.5, det=3, wave="SAW", a=550, rl=900), OFF, OFF],
  eg=[None, EG(a=600, d=1500, s=70, rl=900, c=4)], lfo=[LFO("SIN", 5.2, ks=2, fade=900)], plfo=0.08,
  flt=FLT("LPF12", 2200, reso=10, eg2=10, kh=32, kc=60),
  vp=[VP("MW", "FLT.CUTOFF", 15), VP("VEL", "FLT.CUTOFF", 6)],
  fx=[FX("3EQ", -2, -3, 2, 2500)], sends=(0, 90, 10, 60), pat="PAD",
  gain=3.9, note="four detuned sawtooth carriers (the SAW wave), a slow LPF; MW brighter")
P("SUB BASS", "B", 1, [
    O(100, 1, a=2, rl=80), O(38, 1, a=2, d=400, s=60, rl=80, vel=50),
    O(55, 0.5, a=2, rl=80), OFF, OFF, OFF],
    vp=[VP("MW", "OP2.LEVEL", 25)], fx=[FX("COMP", 100, 50, 40, -3)], sends=(0, 0, 0, 0), mono=1, pat="SUB",
    gain=-8.7, note="a sine, its octave below, a little warmth from op 2; MW growls")
P("FM TOM", "D", 1, [
    O(95, 1, d=450, s=0, rl=300, c=9, vel=50, pe=7), O(58, 1.5, d=90, s=0, rl=90, c=8, vel=60, pe=7),
    O(45, 1, wave="WHITE", d=45, s=0, rl=40, c=8, vel=60), OFF, OFF, OFF],
  eg=[EG(d=110, s=0, rl=100, c=8)], vp=[VP("MW", "OP2.LEVEL", 20)],
  fx=[FX("COMP", 100, 60, 10, -2)], sends=(0, 0, 10, 25), pat="BEAT",
  gain=-6.8, note="a sine and its modulator falling 7 semitones (EG1), a noise click")

# ============================================================================== F13..: Felucca-OPX Solo's bank ====
# ---------------------------------------------------------------------------------------------------------- KEYS --
P("TINE EP", "K", 5, [
    O(95, 1, d=2400, s=0, rl=350, c=8, vel=40), O(58, 1, d=1500, s=0, rl=350, c=8, vel=70, ktrk=(0, -25, 60)),
    O(75, 1, det=3, d=1800, s=0, rl=350, c=8, vel=50), O(46, 14, d=90, s=0, rl=80, c=9, vel=95, ktrk=(0, -40, 60)),
    O(48, 1, det=-3, d=2000, s=0, rl=350, c=8, vel=40), O(50, 1, d=900, s=0, rl=300, c=8, fb=30, vel=60)],
  vp=[VP("MW", "OP4.LEVEL", 20)], fx=[FX("PAN", 30, "SINE", 4.2, 50), FX("3EQ", 1, 1, 0, 1000)],
  sends=(0, 45, 20, 35), pat="KEYS", gain=5.3, note="a bell-tine (14:1) struck by velocity over two mellow pairs")
P("WURLY", "K", 5, [
    O(95, 1, d=2200, s=20, rl=250, c=8, vel=45, ll=18, ll_c="MW"), O(55, 1, d=700, s=10, rl=250, c=8, vel=95),
    O(55, 1, det=4, d=1600, s=10, rl=250, c=8, vel=45, ll=18, ll_c="MW"), O(42, 3, d=250, s=0, rl=200, c=8, vel=90),
    OFF, OFF],
  lfo=[LFO("SIN", 5.6, ks=0)], fx=[FX("3EQ", 1, -2, 2, 900), FX("G.AMP", 25, "AC15", 50, 0)],
  sends=(0, 20, 10, 25), pat="KEYS", gain=-12.3, note="a reed's bark from velocity (op 2, 4); MW the amp's tremolo (LMOD LFO1)")
P("DYNO EP", "K", 5, [
    O(92, 1, d=2600, s=0, rl=400, c=8, vel=40), O(62, 1, d=1200, s=0, rl=300, c=8, vel=75),
    O(70, 1, det=5, d=2200, s=0, rl=400, c=8, vel=40), O(50, 1, det=-4, d=900, s=0, rl=300, c=8, vel=70, fb=20),
    O(50, 2, d=1500, s=0, rl=400, c=8, vel=40), O(55, 14, d=120, s=0, rl=100, c=9, vel=95, ktrk=(0, -35, 60))],
  vp=[VP("MW", "OP6.LEVEL", 18)], fx=[FX("EXC", 40, 6000, 20), FX("PAN", 25, "TRI", 2.5, 50)],
  sends=(0, 70, 20, 30), pat="KEYS", gain=2.3, note="a brighter, chorused EP with an exciter; MW more tine")
P("FM PIANO", "K", 7, [
    O(95, 1, d=4500, s=0, rl=400, c=9, vel=55), O(60, 1, d=1800, s=0, rl=300, c=9, vel=80, ktrk=(10, -30, 60)),
    O(70, 2, det=2, d=3000, s=0, rl=400, c=9, vel=55), O(40, 5, d=150, s=0, rl=100, c=9, vel=90, ktrk=(0, -40, 60)),
    O(45, 1, det=-2, d=2500, s=0, rl=300, c=9, vel=70), O(35, 9, d=60, s=0, rl=50, c=9, vel=90)],
  vp=[VP("MW", "OP2.LEVEL", 12)], fx=[FX("3EQ", 2, 1, -2, 500)], sends=(0, 15, 0, 45), pat="MELODY",
  gain=1.6, note="a struck string: the fundamental's pair, an octave pair with a hammer (5:1, 9:1)")
P("CLAV", "K", 1, [
    O(92, 1, d=900, s=0, rl=60, c=8, vel=40), O(72, 1, d=500, s=15, rl=60, c=8, fb=60, vel=80),
    O(55, 3, d=600, s=0, rl=60, c=8, vel=40), O(48, 1, d=200, s=0, rl=60, c=8, vel=70), OFF, OFF],
  flt=FLT("HPF12", 180, reso=5), vp=[VP("MW", "OP2.FB", 25)],
  fx=[FX("A.WAH", 60, 55, 20, 40)], sends=(0, 0, 10, 15), pat="KEYS",
  gain=15.6, note="a bright, buzzing pair with a 3:1 overtone, an auto-wah; MW more buzz")
P("HARPSI", "K", 5, [
    O(85, 1, d=2500, s=0, rl=300, c=9, vel=10), O(68, 3, d=1500, s=0, rl=300, c=9, vel=30),
    O(75, 2, det=3, d=2000, s=0, rl=300, c=9, vel=10), O(62, 5, d=1000, s=0, rl=300, c=9, vel=30),
    O(40, 1, d=60, s=0, rl=60, c=9), O(60, 11, d=40, s=0, rl=40, c=9)],
  vp=[VP("MW", "OP4.LEVEL", 15)], fx=[FX("3EQ", -3, 4, 0, 1000)], sends=(0, 0, 0, 40), pat="MELODY",
  gain=2.0, note="two bright plucked choirs (8', 4') and a short jack click (11:1)")
P("TOY PIANO", "K", 5, [
    O(95, 1, d=900, s=0, rl=200, c=9, vel=40), O(55, 3.5, d=300, s=0, rl=150, c=9, vel=70),
    O(55, 2, d=500, s=0, rl=200, c=9, vel=40), O(50, 7.1, d=100, s=0, rl=80, c=9, vel=70), OFF, OFF],
  oct=1, vp=[VP("MW", "OP4.LEVEL", 20)], fx=[FX("3EQ", -4, 2, 0, 1000)], sends=(0, 10, 25, 30), pat="BELL",
  gain=14.3, note="struck metal rods: inharmonic 3.5:1 and 7.1:1, an octave up")
P("PERC ORGAN", "K", 32, [
    O(80, 1, a=2, rl=30), O(60, 2, a=2, rl=30), O(55, 4, a=2, rl=30),
    O(72, 2, a=1, d=600, s=0, rl=30, c=9, vel=30), O(50, 0.5, a=2, rl=30), OFF],
  lfo=[LFO("SIN", 6.6)], plfo=0.1, plfo_c="MW", fx=[FX("3EQ", 1, 2, 0, 1000), FX("PAN", 30, "SINE", 6.0, 50)],
  sends=(0, 40, 0, 25), pat="KEYS", gain=3.4, note="8' 4' 2' with a decaying second-harmonic percussion; MW vibrato")
P("ROCK ORGAN", "K", 32, [
    O(85, 0.5, a=1, rl=30), O(85, 1, a=1, rl=30), O(70, 1.5, a=1, rl=30), O(78, 2, a=1, rl=30),
    O(65, 3, a=1, rl=30), O(60, 4, a=1, d=200, s=70, rl=30, c=8)],
  lfo=[LFO("SIN", 6.8)], plfo=0.15, plfo_c="MW",
  fx=[FX("G.AMP", 55, "UK 70S", 60, -4), FX("PAN", 40, "SINE", 6.5, 50)], sends=(0, 30, 0, 25), pat="KEYS",
  gain=-30, note="all six drawbars through an overdriven amp, a fast pan for the rotor; MW vibrato")
P("PIPE ORGAN", "K", 32, [
    O(70, 0.5, a=70, rl=300), O(80, 1, a=60, rl=300), O(60, 2, a=50, rl=300), O(50, 3, a=50, rl=300),
    O(45, 4, a=45, rl=300), O(40, 8, a=40, rl=300, ktrk=(0, -30, 72))],
  vp=[VP("MW", "OP6.LEVEL", 25), VP("MW", "OP5.LEVEL", 20)], fx=[FX("3EQ", 2, 0, 0, 1000)],
  sends=(0, 0, 0, 100), pat="PAD", gain=1.7, note="principal ranks 16' to 1' with a breath of attack; MW the mixture")
P("HARMONIUM", "K", 32, [
    O(70, 1, wave="SQR", a=120, rl=200), O(55, 2, det=4, wave="SAW", a=140, rl=200),
    O(50, 0.5, det=-3, wave="SQR", a=160, rl=200), OFF, OFF, OFF],
  flt=FLT("LPF12", 1600, reso=10, kh=48), lfo=[None, LFO("SIN", 4.5)],
  vp=[VP("MW", "FLT.CUTOFF", 15), VP("LFO2", "PROG.LEVEL", 4, ctrl="MW")], fx=[FX("3EQ", 1, -2, 2, 1200)],
  sends=(0, 30, 0, 45), pat="PAD", gain=7.3, note="reeds: square and saw waves, a slow bellows swell; MW opens, and wavers")
P("SOFT EP", "K", 5, [
    O(95, 1, d=3000, s=0, rl=500, c=8, vel=35), O(45, 1, d=1500, s=0, rl=400, c=8, vel=85),
    O(65, 1, det=4, d=2600, s=0, rl=500, c=8, vel=35), O(40, 14, d=70, s=0, rl=60, c=9, vel=95), OFF, OFF],
  vp=[VP("MW", "OP2.LEVEL", 18)], fx=[FX("PAN", 45, "SINE", 3.0, 50), FX("3EQ", 3, -3, 0, 1000)],
  sends=(0, 50, 25, 45), pat="KEYS", gain=3.2, note="a dark ballad EP, a wide tremolo; MW warmer bark")
P("LO-FI KEYS", "K", 5, [
    O(92, 1, d=2400, s=0, rl=350, c=8, vel=40), O(55, 1, d=1200, s=0, rl=300, c=8, vel=75),
    O(65, 1, det=7, d=2000, s=0, rl=350, c=8, vel=40), O(45, 14, d=80, s=0, rl=80, c=9, vel=90),
    OFF, OFF],
  lfo=[LFO("TRI", 0.6)], vp=[VP("LFO1", "PROG.PITCH", 0.15), VP("MW", "PROG.PITCH", 0.2)],
  fx=[FX("DEC", 60, 9000, 10.0, 30), FX("3EQ", 3, -6, 0, 1000)], sends=(0, 30, 20, 30), pat="KEYS",
  gain=2.5, note="an EP through a 10-bit sampler with tape wow (LFO1 on pitch); MW deeper wow")
P("E.GRAND", "K", 7, [
    O(92, 1, d=3500, s=0, rl=350, c=9, vel=45), O(64, 1, d=1600, s=0, rl=300, c=9, vel=80, ktrk=(0, -30, 60)),
    O(68, 2, det=3, d=2500, s=0, rl=350, c=9, vel=45), O(48, 3, d=400, s=0, rl=200, c=9, vel=85),
    O(42, 1, det=-3, d=1800, s=0, rl=300, c=9, vel=75), O(38, 7, d=80, s=0, rl=60, c=9, vel=90)],
  vp=[VP("MW", "OP4.LEVEL", 15)], fx=[FX("EXC", 35, 5000, 10), FX("PAN", 20, "SINE", 1.0, 50)],
  sends=(0, 60, 10, 35), pat="MELODY", gain=2.7, note="an electric grand: bright octave pair, a chorus of strings")
P("POLY KEYS", "K", 32, [
    O(75, 1, wave="SAW", d=900, s=50, rl=250, c=6, vel=40), O(70, 1, det=9, wave="SQR", d=900, s=50, rl=250, c=6, vel=40),
    O(45, 0.5, d=900, s=50, rl=250, c=6), OFF, OFF, OFF],
  eg=[None, EG(d=450, s=20, rl=300, c=6)], flt=FLT("LPF24", 700, reso=20, eg2=40, eg2_c="VEL", kh=48),
  vp=[VP("MW", "FLT.CUTOFF", 15)], sends=(0, 60, 25, 30), pat="KEYS",
  gain=20.4, note="a saw and a square into a 24 dB filter plucked by velocity; MW opens it")
P("HOUSE ORG", "K", 1, [
    O(88, 1, d=350, s=0, rl=120, c=8, vel=30), O(60, 2, d=250, s=0, rl=120, c=8, vel=50),
    O(70, 2, d=300, s=0, rl=120, c=8, vel=30), O(50, 3, d=200, s=0, rl=120, c=8, fb=30, vel=50),
    O(40, 3, d=150, s=0, rl=120, c=8), O(30, 1, d=150, s=0, rl=120, c=8)],
  vp=[VP("MW", "OP4.LEVEL", 20)], fx=[FX("3EQ", 3, 2, -2, 600), FX("COMP", 100, 60, 20, -2)],
  sends=(0, 10, 30, 30), pat="KEYS", gain=-13.3, note="a short, hollow organ stab for 90s house; MW more body")

# -------------------------------------------------------------------------------- PLUCKS, BELLS AND MALLETS --
P("VIBES", "U", 5, [
    O(92, 1, d=4500, s=0, rl=700, c=9, vel=50, ll=30, ll_c="MW"), O(48, 4, d=180, s=0, rl=200, c=9, vel=80),
    O(55, 4, d=900, s=0, rl=500, c=9, vel=50, ll=30, ll_c="MW"), O(30, 1, d=200, s=0, rl=200, c=9),
    OFF, OFF],
  lfo=[LFO("SIN", 5.0)], fx=[FX("PAN", 25, "SINE", 5.0, 50)], sends=(0, 10, 15, 45), pat="BELL",
  gain=15.6, note="a bar (1:1 with a 4:1 strike) and its 4th partial; MW the motor's tremolo")
P("MARIMBA", "U", 5, [
    O(95, 1, d=900, s=0, rl=250, c=9, vel=50), O(52, 3.93, d=70, s=0, rl=60, c=9, vel=85),
    O(45, 3.93, d=250, s=0, rl=150, c=9, vel=60), O(25, 1, d=60, s=0, rl=60, c=9), OFF, OFF],
  vp=[VP("MW", "OP2.LEVEL", 18)], fx=[FX("3EQ", 3, -2, 0, 1000)], sends=(0, 0, 10, 35), pat="MELODY",
  gain=1.5, note="a tuned wooden bar: the fundamental and its 3.93 mode, a soft mallet")
P("XYLOPHONE", "U", 5, [
    O(92, 1, d=500, s=0, rl=150, c=9, vel=40), O(58, 3, d=60, s=0, rl=50, c=9, vel=80),
    O(55, 3, d=250, s=0, rl=120, c=9, vel=50), O(30, 2, d=40, s=0, rl=40, c=9), OFF, OFF],
  oct=1, vp=[VP("MW", "OP2.LEVEL", 15)], fx=[FX("3EQ", -2, 3, 0, 1000)], sends=(0, 0, 10, 40), pat="MELODY",
  gain=15.6, note="a hard bar: 3:1 partial, a hard mallet, an octave up")
P("GLOCKEN", "U", 31, [
    O(85, 1, d=3500, s=0, rl=800, c=9, vel=40), O(55, 2.76, d=2000, s=0, rl=600, c=9, vel=50),
    O(40, 5.4, d=900, s=0, rl=400, c=9, vel=60), O(30, 8.9, d=400, s=0, rl=300, c=9, vel=60),
    O(40, 1, d=1500, s=0, rl=500, c=9), O(45, 3.5, d=80, s=0, rl=60, c=9, vel=80)],
  oct=1, vp=[VP("MW", "OP6.LEVEL", 20)], sends=(0, 0, 25, 60), pat="BELL",
  gain=4.3, note="the steel bar's partials 1, 2.76, 5.4, 8.9 as carriers, a struck 3.5:1")
P("TUBE BELL", "U", 1, [
    O(92, 1, d=7000, s=0, rl=2000, c=10, vel=40), O(62, 3.5, d=3000, s=0, rl=1500, c=10, vel=60),
    O(70, 2.76, det=5, d=5000, s=0, rl=2000, c=10, vel=40), O(45, 1.41, d=2500, s=0, rl=1500, c=10, vel=50),
    O(30, 1, d=200, s=0, rl=200, c=9), OFF],
  vp=[VP("MW", "OP2.LEVEL", 15)], fx=[FX("3EQ", -2, 1, 2, 600)], sends=(0, 0, 20, 70), pat="BELL",
  gain=1.5, note="a chime: 3.5:1 and 1.41:1 strikes on two partials, slow index decay")
P("CRYSTAL", "U", 5, [
    O(85, 1, d=4000, s=0, rl=1200, c=10, vel=40), O(55, 7, d=1200, s=0, rl=800, c=10, vel=60),
    O(70, 4, det=6, d=3000, s=0, rl=1200, c=10, vel=40), O(48, 1.41, d=1800, s=0, rl=800, c=10, vel=60),
    O(55, 2, det=-6, d=2500, s=0, rl=1200, c=10, vel=40), O(40, 5.19, d=600, s=0, rl=400, c=10, vel=60)],
  vp=[VP("MW", "OP4.LEVEL", 18)], fx=[FX("PAN", 40, "TRI", 0.4, 50)], sends=(0, 20, 45, 85), pat="BELL",
  gain=4.2, note="glass: three high partials, each with an inharmonic modulator")
P("KALIMBA", "U", 5, [
    O(95, 1, d=1400, s=0, rl=300, c=9, vel=50), O(55, 5.4, d=50, s=0, rl=40, c=9, vel=85),
    O(40, 5.4, d=300, s=0, rl=150, c=9, vel=50), O(30, 1, d=30, s=0, rl=30, c=9), OFF, OFF],
  vp=[VP("MW", "OP2.LEVEL", 20)], fx=[FX("3EQ", 3, 0, -2, 500)], sends=(0, 0, 20, 40), pat="MELODY",
  gain=1.7, note="a plucked tine: its 5.4 mode, a sharp click")
P("MUSIC BOX", "U", 5, [
    O(90, 1, d=2200, s=0, rl=1000, c=10, vel=30), O(48, 4, d=400, s=0, rl=300, c=10, vel=60),
    O(55, 3, det=8, d=1200, s=0, rl=800, c=10, vel=30), O(35, 7.03, d=200, s=0, rl=200, c=10),
    OFF, OFF],
  oct=1, vp=[VP("MW", "OP4.LEVEL", 25)], fx=[FX("3EQ", -6, 2, 0, 1000)], sends=(0, 0, 30, 60), pat="BELL",
  gain=9.4, note="a comb's tooth, an octave up; detuned 3rd partial")
P("STEEL PAN", "U", 5, [
    O(92, 1, d=1600, s=0, rl=300, c=9, vel=40, pe=0.4), O(55, 1.41, d=150, s=0, rl=100, c=9, vel=80),
    O(60, 2, d=1100, s=0, rl=300, c=9, vel=40), O(42, 3.2, d=200, s=0, rl=100, c=9, vel=70),
    O(40, 3, d=600, s=0, rl=200, c=9), O(30, 1, d=100, s=0, rl=100, c=9)],
  eg=[EG(d=40, s=0, rl=40, c=8)], vp=[VP("MW", "OP2.LEVEL", 18)], sends=(0, 20, 20, 40), pat="MELODY",
  gain=8.8, note="a hammered pan: a little pitch blip (EG1), the 2nd and 3rd notes of the dome")
P("HARP", "U", 5, [
    O(92, 1, d=3500, s=0, rl=1200, c=9, vel=50), O(45, 1, d=600, s=0, rl=400, c=9, vel=70),
    O(60, 2, d=2000, s=0, rl=1000, c=9, vel=50), O(30, 1, d=300, s=0, rl=200, c=9, vel=60),
    O(35, 3, d=1000, s=0, rl=600, c=9), O(30, 4, d=50, s=0, rl=50, c=9)],
  vp=[VP("MW", "OP2.LEVEL", 15)], fx=[FX("3EQ", 1, -1, 0, 1000)], sends=(0, 25, 10, 60), pat="ARP",
  gain=3.8, note="a plucked gut string with a soft pluck; long release for glissandi")
P("NYLON GTR", "U", 5, [
    O(92, 1, d=2400, s=0, rl=300, c=9, vel=50), O(50, 3, d=300, s=0, rl=200, c=9, vel=75, ktrk=(0, -20, 60)),
    O(60, 1, det=4, d=1800, s=0, rl=300, c=9, vel=50), O(42, 1, d=150, s=0, rl=150, c=9, fb=30, vel=70),
    O(30, 2, d=900, s=0, rl=300, c=9), O(40, 5, d=40, s=0, rl=40, c=9, vel=80)],
  vp=[VP("MW", "OP4.FB", 20)], fx=[FX("3EQ", 2, -3, 0, 1000)], sends=(0, 20, 0, 35), pat="MELODY",
  note="a nylon string: 3:1 body, a soft fingertip (5:1) click")
P("FUZZ GTR", "U", 1, [
    O(90, 1, d=3000, s=40, rl=200, c=7, vel=30), O(64, 1, d=1500, s=40, rl=200, c=7, fb=55, vel=60),
    O(55, 2, det=6, d=2500, s=30, rl=200, c=7, vel=30), O(45, 1, d=800, s=20, rl=200, c=7, vel=60), OFF, OFF],
  lfo=[LFO("SIN", 5.5, ks=2, fade=700)], plfo=0.25, plfo_c="MW",
  fx=[FX("G.AMP", 70, "UK 80S", 55, -8), FX("3EQ", -2, -3, 3, 1500)], sends=(0, 30, 20, 30), mono=1, pat="LEAD",
  gain=-21.1, note="a sawtooth-like pair through a high-gain amp; MW a guitarist's vibrato")
P("KOTO", "U", 5, [
    O(92, 1, d=1800, s=0, rl=300, c=9, vel=50, pe=0.6), O(55, 3, d=200, s=0, rl=150, c=9, vel=80, pe=0.6),
    O(40, 2, d=900, s=0, rl=200, c=9), O(30, 1, d=100, s=0, rl=100, c=9, fb=40), OFF, OFF],
  eg=[EG(d=70, s=0, rl=70, c=8)], lfo=[LFO("SIN", 6.0, ks=2, fade=500)], plfo=0.3, plfo_c="MW",
  fx=[FX("3EQ", -1, 2, 0, 1000)], sends=(0, 10, 15, 45), pat="MELODY",
  gain=17.8, note="a pressed silk string: a pitch flick at the attack (EG1); MW a bent vibrato")
P("PIZZICATO", "U", 32, [
    O(85, 1, d=350, s=0, rl=200, c=9, vel=50), O(70, 1, det=-7, d=320, s=0, rl=200, c=9, vel=50, fb=35),
    O(60, 2, det=5, d=250, s=0, rl=200, c=9, vel=50), OFF, OFF, OFF],
  flt=FLT("LPF12", 2500, kh=32), vp=[VP("MW", "FLT.CUTOFF", 12)], sends=(0, 50, 0, 60), pat="MELODY",
  gain=4.6, note="plucked strings: a section's detuned carriers, short and soft")
P("SITAR", "U", 2, [
    O(80, 1, d=3000, s=0, rl=300, c=8, vel=30, mode="EFFECT", fx=("COMB", 30, 120, 70)),
    O(70, 1, d=2500, s=10, rl=300, c=8, fb=45, vel=60),
    O(45, 2, d=2000, s=0, rl=300, c=8, vel=30), O(35, 3, d=600, s=0, rl=200, c=8),
    O(30, 1, d=300, s=0, rl=200, c=8), OFF],
  lfo=[LFO("SIN", 5.0, ks=2, fade=400)], plfo=0.4, plfo_c="MW", sends=(0, 0, 25, 40), pat="MELODY",
  gain=19.4, note="a buzzing string: op 1 an operator COMB effect over its sawtooth-like input; MW a meend vibrato")
P("FOLD PLUCK", "U", 1, [
    O(85, 1, d=1400, s=0, rl=250, c=8, vel=40, mode="FOLD", fold=(45, 30, 10)),
    O(90, 1, d=260, s=0, rl=200, c=8, vel=80), O(50, 2, d=800, s=0, rl=250, c=8), OFF, OFF, OFF],
  vp=[VP("MW", "OP2.LEVEL", -20)], fx=[FX("3EQ", -1, -2, 0, 1000)], sends=(0, 30, 50, 35), pat="ARP",
  gain=17.3, note="a wave folder fed a decaying sine: the folds fall away; MW fewer folds")
P("RING PLUCK", "U", 1, [
    O(90, 1, d=1600, s=0, rl=300, c=9, vel=40, mode="RING", ring=(80, 20)),
    O(85, 2.5, d=600, s=0, rl=200, c=9, vel=60), O(45, 1, d=900, s=0, rl=250, c=9), OFF, OFF, OFF],
  vp=[VP("MW", "OP2.PITCH", 7)], fx=[FX("PAN", 30, "TRI", 1.5, 50)], sends=(0, 20, 45, 45), pat="MELODY",
  gain=20.5, note="ring modulation by a 2.5:1 sine: a metallic pluck; MW moves the ring's pitch")
P("GAMELAN", "U", 5, [
    O(85, 1, d=5000, s=0, rl=1500, c=10, vel=40), O(50, 2.4, d=900, s=0, rl=600, c=10, vel=70),
    O(85, 1, det=9, d=5000, s=0, rl=1500, c=10, vel=40), O(50, 2.4, d=900, s=0, rl=600, c=10, vel=70),
    O(50, 3.0, d=2500, s=0, rl=1000, c=10, vel=40), O(40, 1.5, d=400, s=0, rl=300, c=10)],
  vp=[VP("MW", "OP5.LEVEL", 20)], sends=(0, 0, 25, 60), pat="BELL",
  gain=-0.7, note="a pair of bronze keys tuned 9 cents apart: the ombak beat")

# --------------------------------------------------------------------------------------------------------- BASS --
P("DX BASS", "B", 1, [
    O(100, 1, d=1200, s=50, rl=80, c=7, vel=30), O(70, 1, d=300, s=30, rl=80, c=7, fb=50, vel=70),
    O(65, 1, d=400, s=20, rl=80, c=7, vel=30), O(55, 3, d=60, s=0, rl=60, c=8, vel=80), OFF, OFF],
  vp=[VP("MW", "OP2.LEVEL", 15)], fx=[FX("COMP", 100, 50, 30, -4)], sends=(0, 0, 0, 5), mono=1, pat="OFFBEAT",
  gain=-6.4, note="a bright sawtooth-like pair with a 3:1 pick; velocity is brightness")
P("SLAP BASS", "B", 5, [
    O(100, 1, d=1500, s=30, rl=80, c=8, vel=40), O(70, 1, d=180, s=15, rl=80, c=8, vel=95),
    O(55, 2, d=300, s=0, rl=80, c=8, vel=60), O(60, 7, d=50, s=0, rl=50, c=9, vel=95),
    O(40, 0.5, d=1000, s=40, rl=80, c=8), OFF],
  vp=[VP("MW", "OP4.LEVEL", 18)], fx=[FX("COMP", 100, 70, 10, -5), FX("3EQ", 2, 3, -3, 500)],
  sends=(0, 0, 0, 5), mono=1, pat="OFFBEAT", gain=-9.1, note="a thumb's slap (hard velocity) and a 7:1 pop")
P("UPRIGHT", "B", 1, [
    O(100, 1, d=1600, s=0, rl=150, c=9, vel=40, pe=-0.3), O(45, 1, d=250, s=0, rl=120, c=9, vel=70),
    O(40, 2, d=900, s=0, rl=150, c=9, vel=40), O(30, 3, d=80, s=0, rl=80, c=9), OFF, OFF],
  eg=[EG(d=80, s=0, rl=80, c=8)], vp=[VP("MW", "OP2.LEVEL", 15)], fx=[FX("3EQ", 2, -4, 0, 1000)],
  sends=(0, 0, 0, 25), mono=1, pat="OFFBEAT", gain=22.8, note="a plucked double bass: a dark pair, a little sag in pitch")
P("FRETLESS", "B", 1, [
    O(100, 1, a=10, d=2500, s=50, rl=150, c=7, vel=30), O(55, 1, a=30, d=800, s=40, rl=150, c=7, vel=60),
    O(35, 2, a=10, d=1500, s=30, rl=150, c=7), OFF, OFF, OFF],
  lfo=[LFO("SIN", 5.0, ks=2, fade=600)], plfo=0.25, plfo_c="MW", fx=[FX("3EQ", 3, -2, 2, 700)],
  sends=(0, 35, 0, 25), mono=1, pat="OFFBEAT", gain=17.7, note="a singing fretless: a soft mwah from op 2's slower attack")
P("PICK BASS", "B", 1, [
    O(100, 1, d=1400, s=40, rl=80, c=7, vel=30), O(62, 1, d=500, s=25, rl=80, c=7, fb=30, vel=60),
    O(45, 1, d=600, s=20, rl=80, c=7), O(55, 5, d=40, s=0, rl=40, c=9, vel=90), OFF, OFF],
  fx=[FX("COMP", 100, 60, 20, -4), FX("3EQ", 2, 1, 2, 1500)], vp=[VP("MW", "OP2.FB", 25)],
  sends=(0, 0, 0, 5), mono=1, pat="OFFBEAT", gain=-9.6, note="a picked electric bass: a 5:1 pick on a growling pair")
P("ACID BASS", "B", 32, [
    O(95, 1, wave="SAW", d=500, s=60, rl=40, c=6), OFF, OFF, OFF, OFF, OFF],
  eg=[None, EG(d=200, s=0, rl=100, c=7)],
  flt=FLT("LPF24", 300, reso=72, eg2=48, eg2_c="VEL", kh=32, kc=48),
  vp=[VP("MW", "FLT.CUTOFF", 25)], fx=[FX("DIST", 35, -10, 1200, 3)], sends=(0, 0, 25, 10), mono=1, pat="ACID",
  gain=-0.6, note="one sawtooth, a resonant 24 dB filter, velocity the envelope's accent; MW opens it")
P("PULSE BASS", "B", 32, [
    O(90, 1, wave="SQR", d=600, s=60, rl=60, c=6, vel=30), O(70, 0.5, d=600, s=80, rl=60, c=6), OFF, OFF, OFF, OFF],
  eg=[None, EG(d=280, s=10, rl=100, c=7)], flt=FLT("LPF24", 400, reso=30, eg2=36, eg2_c="VEL", kh=32, kc=48),
  vp=[VP("MW", "FLT.CUTOFF", 20)], sends=(0, 0, 0, 10), mono=1, pat="OFFBEAT",
  gain=9.3, note="a square and its sub-octave sine, a plucky filter")
P("REESE", "B", 32, [
    O(85, 1, det=-14, wave="SAW", rl=80), O(85, 1, det=14, wave="SAW", rl=80), O(70, 0.5, rl=80),
    OFF, OFF, OFF],
  flt=FLT("LPF24", 700, reso=20, kh=16, kc=48, lfo2=6), lfo=[None, LFO("TRI", 0.25)],
  vp=[VP("MW", "FLT.CUTOFF", 18)], fx=[FX("DIST", 30, -9, 200, 3)], sends=(0, 20, 0, 10), mono=1, pat="SUB",
  gain=-9.2, note="two sawtooths 28 cents apart, a sine sub, a slowly drifting filter")
P("808 SUB", "B", 1, [
    O(100, 1, d=2200, s=0, rl=200, c=8, vel=30, pe=12), O(30, 1, d=100, s=0, rl=100, c=8, vel=50),
    OFF, OFF, OFF, OFF],
  eg=[EG(d=50, s=0, rl=50, c=8)], vp=[VP("MW", "OP2.LEVEL", 35)], fx=[FX("DIST", 45, -9, 100, 4)],
  flt=FLT("HPF12", 22),
  sends=(0, 0, 0, 0), mono=1, pat="SUB", gain=-15.3, note="a long sine with a 12-semitone drop (EG1), saturated; MW grit")
P("WOBBLE", "B", 1, [
    O(100, 1, rl=60), O(70, 1, fb=40, rl=60, ll=-80), O(60, 0.5, rl=60), O(40, 1, rl=60), OFF, OFF],
  lfo=[LFO("SIN", 3.0, ks=1)], vp=[VP("MW", "LFO1.SPEED", 30)], flt=FLT("LPF24", 3000, reso=20, kh=0),
  fx=[FX("DIST", 30, -9, 300, 3)], sends=(0, 0, 0, 5), mono=1, pat="SUB",
  gain=-13.1, note="LFO1 pumps the modulator (LMOD): a wobble; MW speeds it up")
P("FAT BASS", "B", 32, [
    O(80, 1, wave="SAW", d=800, s=70, rl=80, c=6), O(80, 1, det=8, wave="SAW", d=800, s=70, rl=80, c=6),
    O(70, 1, det=-5, wave="SQR", d=800, s=70, rl=80, c=6), OFF, OFF, OFF],
  eg=[None, EG(d=350, s=20, rl=120, c=6)], flt=FLT("LPF24", 350, reso=25, eg2=30, eg2_c="VEL", kh=32, kc=48),
  vp=[VP("MW", "FLT.CUTOFF", 20)], fx=[FX("COMP", 100, 50, 30, -3)], sends=(0, 0, 0, 5), mono=1, pat="OFFBEAT",
  gain=-2.7, note="two sawtooths and a square through a warm 24 dB filter")
P("RUBBER", "B", 1, [
    O(100, 1, d=900, s=40, rl=80, c=7, pe=2), O(55, 1, d=200, s=20, rl=80, c=7, vel=70, pe=2),
    O(35, 2, d=300, s=0, rl=80, c=7), OFF, OFF, OFF],
  eg=[EG(d=90, s=0, rl=80, c=8), EG(d=250, s=0, rl=100, c=6)],
  flt=FLT("LPF12", 900, reso=25, eg2=24, kh=32, kc=48),
  vp=[VP("MW", "OP2.LEVEL", 18)], sends=(0, 0, 0, 5), mono=1, pat="OFFBEAT",
  gain=27.4, note="a bouncing bass: each note falls 2 semitones into place (EG1)")
P("FOLD BASS", "B", 1, [
    O(95, 1, d=1200, s=40, rl=80, c=6, mode="FOLD", fold=(30, 50, 0)), O(80, 1, d=400, s=30, rl=80, c=7, vel=70),
    O(60, 0.5, rl=80), OFF, OFF, OFF],
  vp=[VP("MW", "OP2.LEVEL", 18)], fx=[FX("COMP", 100, 50, 30, -4)], sends=(0, 0, 0, 5), mono=1, pat="OFFBEAT",
  gain=-4.3, note="a wave folder fed by velocity and a decaying sine: the growl falls away; MW more folds")
P("METAL BASS", "B", 1, [
    O(100, 1, d=1200, s=40, rl=80, c=7), O(55, 1.41, d=400, s=15, rl=80, c=7, vel=70),
    O(60, 0.5, rl=80), O(40, 3.5, d=150, s=0, rl=60, c=8, vel=70), OFF, OFF],
  vp=[VP("MW", "OP2.LEVEL", 15)], fx=[FX("COMP", 100, 50, 30, -3)], sends=(0, 0, 10, 10), mono=1, pat="OFFBEAT",
  gain=-8.7, note="an inharmonic 1.41:1 pair over a sub: a clanging bass")
P("ORGAN BASS", "B", 32, [
    O(90, 1, a=2, d=800, s=60, rl=60, c=7), O(55, 2, a=2, d=300, s=20, rl=60, c=7), O(45, 3, a=2, d=200, s=0, rl=60, c=7),
    O(75, 0.5, a=2, rl=60), OFF, OFF],
  vp=[VP("MW", "OP3.LEVEL", 20)], fx=[FX("3EQ", 2, -3, 0, 1000)], sends=(0, 10, 0, 10), mono=1, pat="OFFBEAT",
  gain=3.0, note="a deep-house organ bass: drawbars 16' 8' 4' 2 2/3' with a quick decay")
P("SEQ BASS", "B", 1, [
    O(100, 1, d=260, s=30, rl=60, c=7, vel=40), O(65, 1, d=120, s=0, rl=60, c=8, fb=70, vel=70),
    O(50, 0.5, d=300, s=30, rl=60, c=7), OFF, OFF, OFF],
  eg=[None, EG(d=150, s=0, rl=80, c=7)], flt=FLT("LPF24", 900, reso=35, eg2=36, eg2_c="VEL", kh=32, kc=48),
  vp=[VP("MW", "FLT.CUTOFF", 22)], fx=[FX("3EQ", 6, 3, 3, 400)], sends=(0, 0, 20, 5), mono=1, pat="ACID",
  gain=18.1, note="a tight, plucky sequencer bass")

# --------------------------------------------------------------------------------------------------------- LEADS --
P("SYNC LEAD", "L", 1, [
    O(95, 1, rl=200), O(70, 1, rl=200, pe=19, wave="SAW"), O(45, 1, det=6, rl=200), OFF, OFF, OFF],
  eg=[EG(d=700, s=20, rl=300, c=6)], lfo=[LFO("SIN", 5.5, ks=2, fade=500)], plfo=0.3, plfo_c="MW",
  vp=[VP("MW", "OP2.PITCH", 8)], flt=FLT("LPF24", 6000, reso=10, kh=32),
  fx=[FX("DIST", 20, -8, 3000, 2)], sends=(0, 20, 35, 25), mono=1, pat="LEAD",
  gain=-7.9, note="a fake hard sync: op 2's pitch swept down by EG1; MW raises it and adds vibrato")
P("SQUARE LD", "L", 32, [
    O(80, 1, wave="SQR", rl=150), O(70, 1, det=6, wave="SQR", rl=150), OFF, OFF, OFF, OFF],
  lfo=[LFO("SIN", 5.8, ks=2, fade=400)], plfo=0.35, plfo_c="MW", flt=FLT("LPF12", 3500, reso=15, kh=48),
  vp=[VP("VEL", "FLT.CUTOFF", 8)], sends=(0, 25, 35, 25), mono=1, pat="LEAD",
  gain=3.9, note="two squares, a little apart; MW vibrato")
P("WHISTLE", "L", 1, [
    O(85, 1, a=40, rl=150), O(20, 1, a=40, rl=150), O(25, 1, wave="PINK", a=10, d=150, s=10, rl=100, c=6),
    OFF, OFF, OFF],
  oct=1, lfo=[LFO("SIN", 5.5, ks=2, fade=300)], plfo=0.25, plfo_c="MW", sends=(0, 0, 25, 50), mono=1, pat="LEAD",
  gain=30, note="an almost pure sine an octave up, a breath of noise; MW vibrato")
P("THIN LEAD", "L", 32, [
    O(80, 1, width=35, rl=150), O(70, 1, det=5, width=60, rl=150), O(40, 2, width=50, rl=150), OFF, OFF, OFF],
  lfo=[LFO("TRI", 5.5, ks=2, fade=400)], plfo=0.3, plfo_c="MW", flt=FLT("HPF12", 200, kh=64),
  fx=[FX("PHA", 40, 50, 0.3, 50)], sends=(0, 30, 30, 25), mono=1, pat="LEAD",
  gain=23.9, note="sines squeezed by FM WIDTH into thin pulses; MW vibrato")
P("FOLD LEAD", "L", 1, [
    O(90, 1, rl=200, mode="FOLD", fold=(35, 40, 15)), O(70, 1, rl=200, vel=60), O(40, 2, rl=200), OFF, OFF, OFF],
  lfo=[LFO("SIN", 5.5, ks=2, fade=400)], plfo=0.25, vp=[VP("MW", "OP2.LEVEL", 20)],
  flt=FLT("LPF12", 5000, kh=32), sends=(0, 20, 35, 25), mono=1, pat="LEAD",
  gain=23.9, note="a wave-folded sine: velocity and the MW fold it further")
P("CHIP LEAD", "L", 32, [
    O(80, 1, wave="SQR", rl=60), O(50, 2, wave="SQR", rl=60), OFF, OFF, OFF, OFF],
  lfo=[LFO("TRI", 7.0, ks=2, fade=200)], plfo=0.4, plfo_c="MW", fx=[FX("DEC", 100, 22000, 6.0, 0)],
  sends=(0, 0, 30, 15), mono=1, pat="LEAD", gain=15.6, note="squares at 6 bits; MW a quick vibrato")
P("FAT LEAD", "L", 32, [
    O(80, 1, wave="SAW", rl=200), O(80, 1, det=10, wave="SAW", rl=200), O(75, 1, det=-10, wave="SAW", rl=200),
    O(55, 0.5, wave="SQR", rl=200), OFF, OFF],
  eg=[None, EG(d=500, s=60, rl=300, c=6)], lfo=[LFO("SIN", 5.5, ks=2, fade=600)], plfo=0.3, plfo_c="MW",
  flt=FLT("LPF24", 1500, reso=25, eg2=24, kh=48), fx=[FX("DIST", 20, -7, 1500, 2)], sends=(0, 25, 30, 25),
  mono=1, pat="LEAD", gain=-6.2, note="three sawtooths and a sub square, a 24 dB filter's bite; MW vibrato")
P("MS LEAD", "L", 32, [
    O(85, 1, wave="SAW", rl=200), O(60, 1, det=5, wave="SQR", rl=200), OFF, OFF, OFF, OFF],
  eg=[None, EG(d=400, s=40, rl=300, c=6)], flt=FLT("LPFMS", 1200, reso=70, eg2=30, kh=48),
  vp=[VP("MW", "FLT.CUTOFF", 25)], sends=(0, 10, 35, 20), mono=1, pat="LEAD",
  gain=10.9, note="a saw and a square through a screaming MS-20 filter; MW opens it")
P("BELL LEAD", "L", 5, [
    O(85, 1, rl=200), O(30, 1, rl=200), O(70, 1, det=4, d=1500, s=0, rl=300, c=9, vel=40),
    O(50, 3.5, d=600, s=0, rl=200, c=9, vel=70), O(40, 2, rl=200), O(30, 1, rl=200)],
  lfo=[LFO("SIN", 5.5, ks=2, fade=500)], plfo=0.25, plfo_c="MW", sends=(0, 20, 40, 40), mono=1, pat="LEAD",
  gain=4.6, note="a sine lead with a struck bell in front; MW vibrato")
P("VOX LEAD", "L", 21, [
    O(100, 1, hz=750, rl=200, mode="FILTER", flt=("BPF", 0, 40, 0)),
    O(92, 1, hz=1150, rl=200, mode="FILTER", flt=("BPF", 0, 40, 0)), O(100, 1, wave="SAW", rl=200),
    O(80, 1, hz=2600, rl=200, mode="FILTER", flt=("BPF", 0, 35, 0)),
    O(70, 1, hz=3400, rl=200, mode="FILTER", flt=("BPF", 0, 35, 0)), O(100, 1, det=4, wave="SAW", rl=200)],
  lfo=[LFO("SIN", 5.3, ks=2, fade=500)], plfo=0.3, plfo_c="MW", vp=[VP("MW", "OP2.LEVEL", 20)],
  sends=(0, 30, 30, 40), mono=1, pat="LEAD",
  gain=9.3, note="sawtooths through four fixed band-pass operators (FILTER mode at 750, 1150, 2600, 3400 Hz): an 'ah'")
P("SOFT LEAD", "L", 1, [
    O(90, 1, a=20, rl=250), O(40, 1, a=60, rl=250, fb=20), O(30, 2, a=20, rl=250), OFF, OFF, OFF],
  lfo=[LFO("SIN", 5.2, ks=2, fade=700)], plfo=0.3, vp=[VP("MW", "OP2.LEVEL", 20)], sends=(0, 30, 40, 45),
  mono=1, pat="LEAD", gain=28.3, note="a rounded sine lead with a delayed vibrato; MW brighter")
P("SCREAMER", "L", 1, [
    O(90, 1, rl=200), O(70, 1, rl=200, fb=88), O(50, 2, rl=200, fb=40), OFF, OFF, OFF],
  flt=FLT("LPF24", 2500, reso=30, kh=48), lfo=[LFO("SIN", 6.0, ks=2, fade=400)], plfo=0.35, plfo_c="MW",
  fx=[FX("DIST", 40, -11, 2000, 3)], sends=(0, 20, 35, 25), mono=1, pat="LEAD",
  gain=-5.3, note="a modulator at the noisy edge of its feedback, tamed by a filter; MW vibrato")
P("TRANCE LD", "L", 32, [
    O(70, 1, det=-20, wave="SAW", rl=300), O(70, 1, det=-7, wave="SAW", rl=300), O(70, 1, wave="SAW", rl=300),
    O(70, 1, det=7, wave="SAW", rl=300), O(70, 1, det=20, wave="SAW", rl=300), O(50, 2, wave="SAW", rl=300)],
  flt=FLT("LPF12", 4500, reso=10, kh=32), vp=[VP("MW", "FLT.CUTOFF", 12)], sends=(0, 100, 50, 50), pat="LEAD",
  gain=4.0, note="six sawtooths spread 40 cents (a supersaw), the chorus wide")
P("STAB LEAD", "L", 1, [
    O(95, 1, d=350, s=0, rl=150, c=8, vel=40), O(65, 1, d=200, s=0, rl=150, c=8, fb=50, vel=70),
    O(70, 2, d=300, s=0, rl=150, c=8, vel=40), O(50, 1, d=150, s=0, rl=150, c=8), OFF, OFF],
  vp=[VP("MW", "OP2.FB", 25)], sends=(0, 20, 50, 30), pat="ARP", gain=9.3, note="a short, bright note for arpeggios and riffs")

# ---------------------------------------------------------------------------------------- BRASS AND WINDS (OTHER) --
P("SECTION", "O", 22, [
    O(90, 1, a=60, d=600, s=85, rl=250, c=4, vel=30), O(70, 1, a=90, d=600, s=70, rl=250, c=4, vel=60, fb=40),
    O(85, 1, det=8, a=70, d=600, s=85, rl=250, c=4, vel=30), O(85, 1, det=-8, a=70, d=600, s=85, rl=250, c=4, vel=30),
    O(65, 2, a=60, d=600, s=80, rl=250, c=4, vel=30), O(68, 1, a=110, d=600, s=65, rl=250, c=4, fb=45, vel=60)],
  lfo=[LFO("SIN", 5.5, ks=2, fade=700)], plfo=0.12, vp=[VP("MW", "OP6.LEVEL", 15)],
  flt=FLT("LPF12", 2500, reso=5, kh=48), sends=(0, 40, 10, 45), pat="KEYS",
  gain=-3.2, note="a horn section: detuned carriers, slow-blooming modulators; MW brassier")
P("TRUMPET", "O", 1, [
    O(95, 1, a=30, rl=150, vel=30), O(70, 1, a=70, d=300, s=80, rl=150, c=4, vel=70, fb=30),
    O(40, 2, a=30, rl=150), O(30, 1, a=20, d=60, s=0, rl=60, c=8), OFF, OFF],
  lfo=[LFO("SIN", 5.5, ks=2, fade=600)], plfo=0.2, plfo_c="MW", flt=FLT("LPF12", 3000, kh=48),
  sends=(0, 0, 15, 40), mono=1, pat="LEAD", gain=1.8, note="a bright solo trumpet: the modulator blooms after the attack")
P("FRENCH HRN", "O", 1, [
    O(95, 1, a=60, rl=300), O(50, 1, a=120, d=500, s=80, rl=300, c=4, vel=60), O(30, 2, a=60, rl=300),
    OFF, OFF, OFF],
  lfo=[LFO("SIN", 5.0, ks=2, fade=800)], plfo=0.1, plfo_c="MW", flt=FLT("LPF12", 1200, kh=48),
  vp=[VP("MW", "OP2.LEVEL", 12)], sends=(0, 20, 10, 70), pat="PAD", gain=25.2, note="a mellow horn in a hall")
P("TROMBONE", "O", 1, [
    O(95, 1, a=40, rl=200, pe=-1), O(68, 1, a=90, d=400, s=75, rl=200, c=4, vel=70, fb=35, pe=-1),
    O(40, 2, a=40, rl=200), OFF, OFF, OFF],
  eg=[EG(a=0, d=120, s=0, rl=100, c=6)], oct=-1, lfo=[LFO("SIN", 5.0, ks=2, fade=700)], plfo=0.15, plfo_c="MW",
  flt=FLT("LPF12", 1800, kh=48), sends=(0, 10, 10, 45), mono=1, pat="LEAD",
  gain=1.7, note="a low brass with a scoop up into each note (EG1)")
P("SAXOPHONE", "O", 1, [
    O(92, 1, a=25, rl=150, vel=30), O(65, 1, a=40, d=500, s=70, rl=150, c=4, vel=70, fb=45),
    O(45, 3, a=30, d=300, s=40, rl=150, c=4, vel=60), O(25, 1, wave="PINK", a=10, d=200, s=20, rl=100, c=6),
    OFF, OFF],
  lfo=[LFO("SIN", 5.2, ks=2, fade=500)], plfo=0.2, plfo_c="MW", flt=FLT("LPF12", 2800, reso=15, kh=48),
  vp=[VP("VEL", "FLT.CUTOFF", 6)], sends=(0, 0, 20, 40), mono=1, pat="LEAD",
  gain=11.4, note="a reedy pair with a 3:1 edge and breath noise")
P("CLARINET", "O", 1, [
    O(92, 1, a=40, rl=150), O(55, 2, a=50, d=400, s=85, rl=150, c=4, vel=60), O(25, 1, a=30, rl=150),
    OFF, OFF, OFF],
  lfo=[LFO("SIN", 5.0, ks=2, fade=800)], plfo=0.1, plfo_c="MW", flt=FLT("LPF12", 2600, kh=48),
  vp=[VP("MW", "OP2.LEVEL", 12)], sends=(0, 0, 10, 45), mono=1, pat="LEAD",
  gain=2.2, note="a 2:1 modulator: odd harmonics, the clarinet's hollow tone")
P("FLUTE", "O", 1, [
    O(92, 1, a=60, rl=150), O(30, 1, a=80, rl=150), O(35, 1, wave="PINK", a=20, d=200, s=20, rl=100, c=6,
                                                       mode="FILTER", flt=("BPF", 12, 40, 0)), OFF, OFF, OFF],
  lfo=[LFO("SIN", 5.0, ks=2, fade=600)], plfo=0.2, plfo_c="MW", sends=(0, 10, 15, 50), mono=1, pat="LEAD",
  gain=14.2, note="a soft sine, breath through a band-pass (FILTER mode) an octave above the note")
P("OBOE", "O", 1, [
    O(90, 1, a=30, rl=150), O(62, 1, a=40, d=400, s=80, rl=150, c=4, vel=60), O(35, 4, a=30, rl=150),
    OFF, OFF, OFF],
  lfo=[LFO("SIN", 5.5, ks=2, fade=500)], plfo=0.15, plfo_c="MW", flt=FLT("BPF12", 1500, reso=15, kh=56),
  sends=(0, 0, 10, 45), mono=1, pat="LEAD", gain=25.4, note="a nasal double reed: a band-pass around 1.5 kHz")
P("PAN FLUTE", "O", 1, [
    O(92, 1, a=50, rl=250), O(20, 2, a=50, rl=250), O(45, 1, wave="WHITE", a=5, d=90, s=5, rl=80, c=8,
                                                       mode="FILTER", flt=("BPF", 0, 50, 0)), OFF, OFF, OFF],
  lfo=[LFO("SIN", 5.0, ks=2, fade=700)], plfo=0.15, plfo_c="MW", sends=(0, 0, 25, 70), mono=1, pat="LEAD",
  gain=8.2, note="a breathy sine with a chiff of noise at the note's own pitch")
P("HARMONICA", "O", 1, [
    O(90, 1, a=20, rl=120), O(60, 1, a=30, rl=120, fb=55, vel=60), O(40, 2, a=20, rl=120), OFF, OFF, OFF],
  lfo=[LFO("TRI", 6.0, ks=2)], vp=[VP("LFO1", "PROG.LEVEL", 10, ctrl="MW")], flt=FLT("BPF12", 1800, reso=10, kh=56),
  sends=(0, 0, 10, 30), mono=1, pat="LEAD", gain=23.9, note="a buzzy reed in a band-pass; MW a hand tremolo")

# ------------------------------------------------------------------------------------------- PADS AND STRINGS --
P("WARM PAD", "P", 32, [
    O(75, 1, det=-8, wave="SAW", a=800, rl=1500), O(75, 1, det=8, wave="SAW", a=900, rl=1500),
    O(50, 0.5, a=900, rl=1500), OFF, OFF, OFF],
  eg=[None, EG(a=1500, d=3000, s=60, rl=1500, c=4)], flt=FLT("LPF12", 900, reso=10, eg2=12, kh=32),
  vp=[VP("MW", "FLT.CUTOFF", 15)], sends=(0, 70, 20, 70), pat="PAD", gain=4.7, note="two detuned sawtooths, slow and warm")
P("BRASS PAD", "P", 32, [
    O(75, 1, det=-6, wave="SAW", a=200, rl=900), O(70, 1, det=6, wave="SQR", a=220, rl=900),
    O(55, 2, det=3, wave="SAW", a=240, rl=900), OFF, OFF, OFF],
  eg=[None, EG(a=500, d=1500, s=50, rl=900, c=4)], flt=FLT("LPF24", 700, reso=15, eg2=24, eg2_c="VEL", kh=32),
  vp=[VP("MW", "FLT.CUTOFF", 15)], fx=[FX("PHA", 30, 50, 0.2, 30)], sends=(0, 60, 15, 60), pat="PAD",
  gain=15.1, note="a saw and a square swelling through a 24 dB filter: an 80s polysynth pad")
P("GLASS PAD", "P", 5, [
    O(85, 1, a=600, rl=2000), O(45, 3, a=900, d=3000, s=60, rl=2000, c=5),
    O(65, 2, det=5, a=700, rl=2000), O(35, 7, a=1000, d=4000, s=50, rl=2000, c=5),
    O(50, 1, det=-5, a=800, rl=2000), O(30, 1.41, a=1200, rl=2000)],
  vp=[VP("MW", "OP4.LEVEL", 20)], fx=[FX("PAN", 40, "SINE", 0.2, 50)], sends=(0, 30, 30, 80), pat="PAD",
  gain=5.6, note="three slow FM pairs with glassy 3:1, 7:1, 1.41:1 overtones")
P("CHOIR", "P", 22, [
    O(85, 1, a=500, rl=1200), O(25, 1, a=600, rl=1200),
    O(65, 1, det=7, a=550, rl=1200, mode="FILTER", flt=("BPF", 0, 60, 100)),
    O(65, 1, det=-7, a=600, rl=1200), O(45, 2, det=3, a=650, rl=1200), O(35, 1, a=600, rl=1200, wave="SAW")],
  lfo=[LFO("SIN", 4.8, ks=2, fade=800)], plfo=0.12, flt=FLT("LPF12", 2400, kh=32),
  vp=[VP("MW", "OP6.LEVEL", 20)], sends=(0, 80, 10, 80), pat="PAD",
  gain=-3.1, note="voices: detuned sines with a breathy saw modulator, a slow vibrato")
P("DIGI PAD", "P", 5, [
    O(80, 1, a=400, rl=1500, width=50), O(55, 2, a=600, d=3000, s=50, rl=1500, c=5),
    O(70, 1, det=6, a=500, rl=1500, width=70), O(45, 3, a=700, rl=1500), O(50, 2, a=600, rl=1500),
    O(35, 5, a=800, d=2000, s=40, rl=1500, c=5)],
  vp=[VP("MW", "OP2.LEVEL", 20), VP("MW", "OP4.LEVEL", 20)], fx=[FX("DEC", 20, 16000, 14.0, 0)],
  sends=(0, 50, 30, 70), pat="PAD", gain=3.8, note="narrowed sines (WIDTH) under FM: a digital pad; MW its dynamics")
P("SWEEP PAD", "P", 32, [
    O(75, 1, det=-7, wave="SAW", a=500, rl=1500), O(75, 1, det=7, wave="SAW", a=500, rl=1500),
    O(55, 2, wave="SQR", a=600, rl=1500), OFF, OFF, OFF],
  lfo=[None, LFO("TRI", 0.12)], flt=FLT("LPF24", 800, reso=55, lfo2=24, kh=32),
  vp=[VP("MW", "LFO2.SPEED", 30)], sends=(0, 60, 30, 70), pat="PAD",
  gain=15.2, note="a resonant filter swept slowly by LFO2; MW speeds the sweep")
P("RANDOM PAD", "P", 32, [
    O(75, 1, det=-5, wave="SAW", a=300, rl=1500), O(75, 1, det=5, wave="SAW", a=300, rl=1500),
    O(50, 0.5, wave="TRI", a=300, rl=1500), OFF, OFF, OFF],
  lfo=[None, LFO("SH", 6.0)], flt=FLT("LPF24", 1200, reso=45, lfo2=18, kh=32),
  vp=[VP("MW", "LFO2.SPEED", -40)], sends=(0, 50, 50, 70), pat="PAD",
  gain=18.1, note="a sample-and-hold LFO2 steps the resonant filter: a pulsing, random pad; MW slows it")
P("SLOW STRGS", "P", 32, [
    O(72, 1, det=-10, wave="SAW", a=1200, rl=1800), O(72, 1, det=10, wave="SAW", a=1300, rl=1800),
    O(65, 0.5, det=-4, wave="SAW", a=1400, rl=1800), O(55, 0.5, det=5, wave="SAW", a=1500, rl=1800), OFF, OFF],
  lfo=[LFO("SIN", 5.0, ks=2, fade=1200)], plfo=0.08, flt=FLT("LPF12", 1400, reso=5, kh=32),
  vp=[VP("MW", "FLT.CUTOFF", 15)], sends=(0, 90, 10, 85), pat="PAD", gain=8.8, note="a dark, slow string section in octaves")
P("CELLO", "O", 1, [
    O(95, 1, a=150, rl=300), O(62, 1, a=200, d=600, s=80, rl=300, c=4, vel=60, fb=40), O(35, 2, a=150, rl=300),
    OFF, OFF, OFF],
  oct=-1, lfo=[LFO("SIN", 5.2, ks=2, fade=500)], plfo=0.2, plfo_c="MW", flt=FLT("LPF12", 2000, reso=10, kh=48),
  sends=(0, 10, 10, 60), mono=1, pat="LEAD", gain=12.3, note="a bowed solo string: a bloom of brightness after the bow")
P("VIOLINS", "P", 32, [
    O(70, 1, det=-9, wave="SAW", a=250, rl=700), O(70, 1, det=9, wave="SAW", a=280, rl=700),
    O(60, 1, det=-3, wave="SAW", a=300, rl=700), O(45, 2, det=4, wave="SAW", a=300, rl=700), OFF, OFF],
  lfo=[LFO("SIN", 5.8, ks=2, fade=600)], plfo=0.1, flt=FLT("LPF12", 3500, reso=10, kh=32),
  vp=[VP("MW", "FLT.CUTOFF", 12)], fx=[FX("3EQ", -3, 2, 2, 3000)], sends=(0, 90, 10, 70), pat="PAD",
  gain=7.2, note="a brighter, faster string section with vibrato")
P("MS STRINGS", "P", 32, [
    O(75, 1, det=-8, wave="SAW", a=350, rl=1000), O(75, 1, det=8, wave="SAW", a=350, rl=1000), OFF, OFF, OFF, OFF],
  eg=[None, EG(a=600, d=2000, s=50, rl=1000, c=4)], flt=FLT("LPFMS", 900, reso=40, eg2=18, kh=32),
  vp=[VP("MW", "FLT.CUTOFF", 15)], sends=(0, 80, 10, 70), pat="PAD",
  gain=4.0, note="string synth through the MS-20 low-pass, a little reso")
P("AIR PAD", "P", 21, [
    O(70, 1, a=1500, rl=2000, mode="FILTER", flt=("BPF", 12, 50, 0)),
    O(65, 1, a=1500, rl=2000, mode="FILTER", flt=("BPF", 19, 50, 0)), O(95, 1, wave="WHITE", a=1500, rl=2000),
    O(75, 1, a=1200, rl=2000), O(55, 2, det=4, a=1200, rl=2000), O(25, 1, a=1500, rl=2000)],
  lfo=[None, None, LFO("TRI", 0.1)], vp=[VP("LFO3", "OP1.PITCH", 0.5), VP("MW", "OP3.LEVEL", 15)],
  sends=(0, 60, 40, 90), pat="PAD",
  gain=6.6, note="noise through band-pass operators tuned an octave and a twelfth up, over soft sines")
P("DARK PAD", "P", 32, [
    O(75, 0.5, det=-6, wave="SAW", a=1500, rl=2500), O(75, 0.5, det=6, wave="SAW", a=1500, rl=2500),
    O(60, 1, wave="TRI", a=1800, rl=2500), OFF, OFF, OFF],
  lfo=[None, LFO("TRI", 0.08)], flt=FLT("LPF24", 500, reso=25, lfo2=6, kh=16),
  vp=[VP("MW", "FLT.CUTOFF", 15)], fx=[FX("3EQ", 6, 6, 6, 1000)], sends=(0, 70, 30, 90), pat="PAD", gain=13.4, note="low sawtooths under a closed filter")
P("SHIMMER", "P", 5, [
    O(80, 1, a=700, rl=2500), O(35, 2, a=900, rl=2500), O(60, 2, det=5, a=900, rl=2500),
    O(40, 7, a=1200, d=3000, s=50, rl=2500, c=5), O(50, 4, det=-6, a=1000, rl=2500),
    O(35, 1, a=1200, rl=2500, ll=30)],
  lfo=[LFO("TRI", 3.0)], vp=[VP("MW", "OP4.LEVEL", 20)], fx=[FX("PAN", 50, "TRI", 0.3, 50)],
  sends=(0, 30, 60, 100), pat="PAD", gain=10.0, note="high FM partials shimmering (LMOD LFO1 on op 6), a long reverb")
P("FOLD PAD", "P", 1, [
    O(80, 1, a=800, rl=2000, mode="FOLD", fold=(25, 40, 0)), O(75, 1, a=900, rl=2000, ll=40),
    O(60, 1, det=7, a=900, rl=2000), O(40, 2, a=1000, rl=2000), OFF, OFF],
  lfo=[LFO("TRI", 0.25)], flt=FLT("LPF12", 2500, kh=32), vp=[VP("MW", "LFO1.SPEED", 30)],
  sends=(0, 50, 30, 80), pat="PAD", gain=3.6, note="a wave folder breathing as LFO1 moves its input (LMOD); MW faster")
P("RING PAD", "P", 1, [
    O(75, 1, a=900, rl=2000, mode="RING", ring=(60, 0)), O(80, 1.005, a=900, rl=2000),
    O(65, 1, det=-5, a=900, rl=2000), O(40, 3, a=1200, rl=2000), OFF, OFF],
  vp=[VP("MW", "OP2.PITCH", 0.3)], sends=(0, 50, 30, 80), pat="PAD",
  gain=8.1, note="ring modulation by a slightly sharp copy: a slow, throbbing beat; MW faster")
P("HALO", "P", 5, [
    O(80, 1, a=400, rl=2500), O(30, 1, a=600, rl=2500), O(70, 1, det=4, d=4000, s=30, rl=2500, c=9, vel=40),
    O(48, 3.5, d=1500, s=10, rl=1500, c=9, vel=60), O(55, 2, det=-4, a=800, rl=2500), O(25, 1, a=800, rl=2500)],
  vp=[VP("MW", "OP4.LEVEL", 18)], fx=[FX("PAN", 30, "SINE", 0.25, 50)], sends=(0, 50, 40, 90), pat="PAD",
  gain=5.5, note="a bell's strike that turns into a pad")
P("POLY SYNTH", "P", 32, [
    O(75, 1, det=-6, wave="SAW", d=1200, s=60, rl=500, c=6), O(75, 1, det=6, wave="SAW", d=1200, s=60, rl=500, c=6),
    OFF, OFF, OFF, OFF],
  eg=[None, EG(d=600, s=30, rl=500, c=6)], flt=FLT("LPF24", 800, reso=20, eg2=30, eg2_c="VEL", kh=32),
  vp=[VP("MW", "FLT.CUTOFF", 15)], sends=(0, 60, 25, 50), pat="KEYS",
  gain=7.5, note="two sawtooths and a velocity-driven filter: an all-purpose poly")

# --------------------------------------------------------------------------------------------------------- DRUMS --
P("FM KICK", "D", 1, [
    O(100, 1, d=600, s=0, rl=200, c=9, vel=40, pe=24), O(40, 1, d=40, s=0, rl=40, c=9, vel=60, pe=24),
    OFF, OFF, OFF, OFF],
  eg=[EG(d=45, s=0, rl=45, c=9)], vp=[VP("MW", "OP2.LEVEL", 25)], fx=[FX("DIST", 20, -6, 80, 3)],
  flt=FLT("HPF12", 22),
  sends=(0, 0, 0, 0), pat="BEAT", gain=-9.5, note="a sine falling two octaves in 45 ms (EG1); play it low; MW more click")
P("SNARE", "D", 1, [
    O(70, 1, hz=185, d=120, s=0, rl=80, c=9, vel=50, pe=5), O(40, 1, hz=330, d=60, s=0, rl=60, c=9),
    O(85, 1, hz=2000, wave="WHITE", d=180, s=0, rl=120, c=8, vel=50, mode="FILTER", flt=("HPF", 0, 10, 100)),
    OFF, OFF, OFF],
  eg=[EG(d=30, s=0, rl=30, c=9)], vp=[VP("MW", "OP3.LEVEL", 15)], fx=[FX("COMP", 100, 60, 10, -2)],
  sends=(0, 0, 0, 30), pat="BEAT", gain=-5.3, note="a 185 Hz body and noise through a 2 kHz high-pass (FILTER mode)")
P("CL HAT", "D", 32, [
    O(80, 1, hz=8000, wave="WHITE", d=55, s=0, rl=50, c=9, vel=50, mode="FILTER", flt=("HPF", 0, 20, 100)),
    O(35, 1, hz=5400, wave="SQR", d=40, s=0, rl=40, c=9), O(30, 1, hz=7900, wave="SQR", d=40, s=0, rl=40, c=9),
    OFF, OFF, OFF],
  vp=[VP("MW", "OP1.DEC", 15)], sends=(0, 0, 0, 10), pat="BEAT",
  gain=30, note="noise over 8 kHz and two metallic squares; MW longer")
P("OPEN HAT", "D", 32, [
    O(80, 1, hz=7500, wave="WHITE", d=450, s=0, rl=300, c=8, vel=50, mode="FILTER", flt=("HPF", 0, 20, 100)),
    O(35, 1, hz=5400, wave="SQR", d=350, s=0, rl=300, c=8), O(30, 1, hz=7900, wave="SQR", d=350, s=0, rl=300, c=8),
    O(25, 1, hz=6300, wave="SQR", d=300, s=0, rl=300, c=8), OFF, OFF],
  vp=[VP("MW", "OP1.DEC", 15)], sends=(0, 0, 0, 15), pat="BEAT", gain=29.9, note="the open hat: the same, ringing")
P("CLAP", "D", 32, [
    O(95, 1, hz=1000, wave="WHITE", d=220, s=0, rl=150, c=8, vel=50, ll=-90, ll_c="EG3"), OFF, OFF, OFF, OFF, OFF],
  eg=[None, None, EG(d=70, s=0, rl=50, c=0)], lfo=[LFO("SAWD", 40.0, ks=2)], flt=FLT("BPF12", 1200, reso=20),
  vp=[VP("MW", "OP1.DEC", 15)], fx=[FX("DIST", 30, -5, 1500, 3)], sends=(0, 0, 0, 40), pat="BEAT",
  gain=-0.5, note="band-passed noise; LFO1 (a fast sawtooth) chops its first 70 ms (LMOD, its CTRL EG3): the hands' flam")
P("COWBELL", "D", 32, [
    O(70, 1, hz=540, wave="SQR", d=350, s=0, rl=150, c=9), O(60, 1, hz=800, wave="SQR", d=300, s=0, rl=150, c=9),
    OFF, OFF, OFF, OFF],
  flt=FLT("BPF12", 900, reso=25), sends=(0, 0, 10, 20), pat="BEAT",
  vp=[VP("MW", "FLT.CUTOFF", 10)], gain=13.4, note="two squares at 540 and 800 Hz through a band-pass")
P("RIM", "D", 1, [
    O(80, 1, hz=1700, d=30, s=0, rl=30, c=9, vel=50), O(50, 1, hz=500, d=20, s=0, rl=20, c=9),
    O(50, 1, hz=460, d=40, s=0, rl=40, c=9), OFF, OFF, OFF],
  vp=[VP("MW", "OP2.LEVEL", 15)], fx=[FX("3EQ", 6, 6, 6, 1500)], sends=(0, 0, 0, 30), pat="BEAT", gain=18.1, note="a short wooden click")
P("METAL HIT", "D", 1, [
    O(90, 1, d=3000, s=0, rl=1500, c=10, vel=40), O(70, 1.41, d=1500, s=0, rl=1000, c=10, vel=70),
    O(70, 2.76, d=2500, s=0, rl=1200, c=10, vel=40), O(65, 5.19, d=800, s=0, rl=600, c=10, vel=70),
    O(55, 3.17, d=500, s=0, rl=400, c=10, fb=50), OFF],
  vp=[VP("MW", "OP2.LEVEL", 15)], sends=(0, 0, 30, 70), pat="BELL",
  gain=9.3, note="a crash of inharmonic metal: deep, slowly decaying modulation")

# ------------------------------------------------------------------------------------------------------------ FX --
P("RISER", "F", 1, [
    O(80, 1, a=4000, rl=800, pe=-24), O(60, 1, a=4000, rl=800, pe=-24, fb=40), O(50, 1, wave="WHITE", a=4000, rl=800),
    OFF, OFF, OFF],
  eg=[EG(a=4000, s=100, rl=800, c=0)], lfo=[None, LFO("TRI", 0.2)],
  flt=FLT("LPF24", 300, reso=40, eg2=0, kh=0), vp=[VP("EG1", "FLT.CUTOFF", 60), VP("MW", "FLT.RESO", 30)],
  sends=(0, 30, 50, 70), pat="PAD", gain=18.1, note="two octaves up in 4 s (EG1 on pitch and cutoff) with noise")
P("LASER", "F", 1, [
    O(90, 1, d=600, s=0, rl=300, c=8, pe=48), O(60, 1, d=600, s=0, rl=300, c=8, pe=48, fb=50), OFF, OFF, OFF, OFF],
  eg=[EG(d=300, s=0, rl=300, c=6)], vp=[VP("MW", "EG1.DEC", 30)], sends=(0, 0, 50, 40), pat="MELODY",
  gain=2.8, note="four octaves down in 300 ms; MW a longer zap")
P("SPACE WIND", "F", 32, [
    O(95, 1, wave="PINK", a=2000, rl=3000), O(60, 1, wave="WHITE", a=2500, rl=3000), OFF, OFF, OFF, OFF],
  lfo=[None, LFO("TRI", 0.12), LFO("TRI", 0.07)], flt=FLT("BPF12", 800, reso=70, lfo2=18),
  vp=[VP("LFO3", "FLT.RESO", 15), VP("MW", "LFO2.SPEED", 30)], fx=[FX("3EQ", 6, 6, 12, 800)], sends=(0, 40, 40, 100), pat="PAD",
  gain=13.4, note="noise through a resonant band-pass that LFO2 sweeps and LFO3 sharpens; MW faster gusts")
P("DRONE", "F", 32, [
    O(90, 0.5, a=3000, rl=4000), O(85, 1, det=3, a=3000, rl=4000, wave="TRI"), O(80, 1.5, det=-4, a=3500, rl=4000),
    O(75, 2, det=5, a=4000, rl=4000, wave="TRI"), O(65, 3, det=-6, a=4500, rl=4000), O(60, 4, a=5000, rl=4000)],
  lfo=[LFO("TRI", 0.07)], vp=[VP("LFO1", "OP5.LEVEL", 20), VP("MW", "OP6.LEVEL", 25)],
  fx=[FX("3EQ", 6, 6, 6, 1000), FX("PHA", 40, 60, 0.05, 50)], sends=(0, 50, 30, 100), pat="PAD",
  gain=13.4, note="a slowly beating stack of root, fifths and octaves")
P("COMPUTER", "F", 1, [
    O(85, 1, rl=50, pl=12), O(55, 2, rl=50, pl=12), OFF, OFF, OFF, OFF],
  lfo=[LFO("SH", 10.0, ks=1)], vp=[VP("MW", "LFO1.SPEED", 30)], sends=(0, 0, 40, 30), pat="PAD",
  gain=3.8, note="a sample-and-hold LFO1 steps the pitch: a 70s computer at work; MW faster")
P("UFO", "F", 1, [
    O(90, 1, rl=400, pl=4), O(55, 1.5, rl=400, pl=4, fb=40), OFF, OFF, OFF, OFF],
  lfo=[LFO("TRI", 7.0, ks=2)], vp=[VP("MW", "LFO1.SPEED", 40)], fx=[FX("PHA", 50, 70, 0.5, 60)],
  sends=(0, 20, 40, 60), pat="PAD", gain=2.8, note="a fast, wide vibrato on an FM tone; MW faster")
P("RUMBLE", "F", 32, [
    O(95, 1, wave="PINK", a=600, rl=2500), O(60, 0.5, a=500, rl=2500, wave="TRI"), OFF, OFF, OFF, OFF],
  flt=FLT("BPF12", 140, reso=25, kh=16, lfo2=8), lfo=[None, LFO("SH", 2.0)], vp=[VP("MW", "FLT.CUTOFF", 25)],
  sends=(0, 0, 20, 70), pat="PAD", gain=12.7, note="low noise and a sub, a filter that stumbles (LFO2 S/H); MW opens it")
P("GLITCH", "F", 1, [
    O(85, 1, d=200, s=50, rl=100, c=6, ll=-100), O(70, 3.5, rl=100, pl=7, fb=50), OFF, OFF, OFF, OFF],
  lfo=[LFO("SH", 16.0, ks=2)], fx=[FX("DEC", 50, 6000, 6.0, 0)], vp=[VP("MW", "LFO1.SPEED", 25)],
  sends=(0, 0, 40, 20), pat="MELODY", gain=4.3, note="random steps on level and pitch, crushed to 6 bits")

# ----------------------------------------------------------------------------------- CHORDS AND SEQUENCES --
P("DUB CHORD", "O", 32, [
    O(70, 1, d=300, s=0, rl=200, c=8), O(65, 1.1892, d=300, s=0, rl=200, c=8), O(65, 1.4983, d=300, s=0, rl=200, c=8),
    O(60, 1.7818, d=300, s=0, rl=200, c=8), OFF, OFF],
  eg=[None, EG(d=180, s=0, rl=150, c=7)], flt=FLT("LPF24", 500, reso=30, eg2=20, eg2_c="VEL", kh=0),
  vp=[VP("MW", "FLT.CUTOFF", 20)], sends=(0, 30, 90, 60), pat="KEYS",
  gain=13.4, note="a minor-seventh chord on every key (carriers at 1, 1.19, 1.50, 1.78), filtered, into the delay")
P("HOUSE STAB", "O", 22, [
    O(75, 1, d=400, s=0, rl=150, c=8), O(50, 1, d=200, s=0, rl=150, c=8, vel=60),
    O(70, 1.1892, d=400, s=0, rl=150, c=8), O(70, 1.4983, d=400, s=0, rl=150, c=8),
    O(65, 2.2449, d=400, s=0, rl=150, c=8), O(45, 1, d=200, s=0, rl=150, c=8, vel=60)],
  vp=[VP("MW", "OP6.LEVEL", 20)], fx=[FX("3EQ", -3, 2, 2, 1200)], sends=(0, 20, 50, 40), pat="KEYS",
  gain=8.2, note="a minor-ninth stab (1, 1.19, 1.50, 2.24), FM-brightened")
P("FIFTHS", "O", 32, [
    O(75, 1, wave="SAW", d=600, s=40, rl=300, c=6), O(70, 1.4983, wave="SAW", d=600, s=40, rl=300, c=6),
    O(55, 2, wave="SAW", d=600, s=40, rl=300, c=6), OFF, OFF, OFF],
  eg=[None, EG(d=300, s=20, rl=200, c=6)], flt=FLT("LPF24", 900, reso=30, eg2=30, eg2_c="VEL", kh=32),
  vp=[VP("MW", "FLT.CUTOFF", 18)], fx=[FX("3EQ", 6, 6, 6, 1000)], sends=(0, 30, 40, 40), pat="ARP",
  gain=15.6, note="root, fifth and octave sawtooths: power chords and parallel lines")
P("GATED PAD", "O", 32, [
    O(75, 1, det=-6, wave="SAW", rl=300, ll=-100), O(75, 1, det=6, wave="SAW", rl=300, ll=-100),
    O(50, 2, rl=300, ll=-100), OFF, OFF, OFF],
  lfo=[LFO("SQRP", 2.0, ks=1, sync="1/16")], flt=FLT("LPF12", 2500, reso=10, kh=32),
  vp=[VP("MW", "FLT.CUTOFF", 15)], sends=(0, 60, 40, 50), pat="PAD",
  gain=13.2, note="a pad chopped in sixteenths by a tempo-synced square (LMOD LFO1)")
P("BLIP", "U", 1, [
    O(85, 1, d=90, s=0, rl=60, c=9, vel=40), O(45, 2, d=40, s=0, rl=40, c=9, vel=70), OFF, OFF, OFF, OFF],
  vp=[VP("MW", "OP2.LEVEL", 25)], fx=[FX("3EQ", 6, 6, 6, 1500)], sends=(0, 0, 60, 30), pat="ARP", gain=4.3, note="a tiny, pure blip for sequences")
P("WOOD SEQ", "U", 5, [
    O(90, 1, d=250, s=0, rl=100, c=9, vel=40), O(55, 3.1, d=50, s=0, rl=40, c=9, vel=80),
    O(55, 2, d=180, s=0, rl=100, c=9, vel=40), O(40, 4.3, d=40, s=0, rl=40, c=9, vel=80), OFF, OFF],
  flt=FLT("HPF12", 120), vp=[VP("MW", "OP4.LEVEL", 20)], fx=[FX("3EQ", 6, 6, 6, 1500), FX("PAN", 40, "SQUARE", 2.0, 50)],
  sends=(0, 0, 55, 30), pat="ARP", gain=15.6, note="a hollow wooden tick that bounces between the speakers")
