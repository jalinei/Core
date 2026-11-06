#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file compare_board.py
## @brief Run the model at a board measurement's conditions and compare (Model tab, hardware column).
## @par Usage
##   .venv/bin/python tests/l1/model_validation/compare_board.py tests/l1/model_validation/board/test11.csv --load-ohm 3000 [--legs 1 | --legs 1,2]
## @par What it does
##   For each duty of the board file: the boost in open loop at the board's input voltage and load,
##   bus precharged to the measured value (so a run settles fast), 150 ms; settled bus and input
##   current compared with the measurement and with the analytic 6·Vin/(1−D).
##   Writes tests/out/<date>_board-<file>_<legs>/comparison.json and prints a table. INFO only.
"""The board is the reference here; the comparison is evidence for the model, not a verdict."""
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor
import argparse,csv,datetime as dt,json,os,sys

HERE=Path(__file__).resolve().parent;REPO=HERE.parents[2]
CALLER=Path.cwd()
sys.path.insert(0,str(HERE));import test_model_open_loop as T  # noqa: E402  (changes directory to tests/l1)


def main():
    a=argparse.ArgumentParser();a.add_argument('board',type=Path);a.add_argument('--load-ohm',type=float,required=True)
    a.add_argument('--legs',default='1,2');a.add_argument('--jobs',type=int,default=os.cpu_count())
    args=a.parse_args();board=args.board if args.board.is_absolute() else CALLER/args.board
    rows=[r for r in csv.DictReader(l for l in board.open() if not l.startswith('#'))]
    legs=[int(x) for x in args.legs.split(',')]
    out=REPO/'tests/out'/f'{dt.date.today()}_board-{board.stem}_legs{"".join(map(str,legs))}';out.mkdir(parents=True,exist_ok=True)
    specs=[{'case':'board','name':f'd{float(r["D_set"]):.3f}','kind':'boost','legs':legs,'duty':float(r['D_set']),'vin_V':float(r['Vin_V']),
            'load_ohm':args.load_ohm,'load_W':0,'initial_bus_V':float(r['Vout_meas_V']),'dt_s':50e-9,'duration_s':0.15,'store_every':100} for r in rows]
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        ms=list(pool.map(T.run_one,specs,[str(out/'runs'/s['name']) for s in specs]))
    table=[]
    print(f'{"D_set":>6} {"D_eff":>6} {"board V":>8} {"model V":>8} {"model-board":>11} {"analytic V":>10} {"board Iin":>9} {"model Iin":>9}')
    for r,s,m in zip(rows,specs,ms):
        vb=float(r['Vout_meas_V']);iin=m['P_in']/m['VLow'] if m['VLow'] else None;analytic=6*float(r['Vin_V'])/(1-m['duty_eff'])
        table.append({'D_set':s['duty'],'D_eff_model':m['duty_eff'],'board_V':vb,'model_V':m['bus_V'],'model_minus_board_pct':100*(m['bus_V']-vb)/vb,
                      'analytic_V':analytic,'board_Iin_A':float(r['Iin_A']) if r['Iin_A'] else None,'model_Iin_A':iin,'settled':m['settled']})
        t=table[-1]
        print(f'{t["D_set"]:6.3f} {t["D_eff_model"]:6.3f} {vb:8.1f} {t["model_V"]:8.1f} {t["model_minus_board_pct"]:+10.1f}% {analytic:10.1f} '
              f'{(t["board_Iin_A"] or float("nan")):9.3f} {iin:9.3f}{"" if t["settled"] else "  (not settled)"}')
    (out/'comparison.json').write_text(json.dumps({'board_file':str(board.relative_to(REPO)),'load_ohm':args.load_ohm,'legs':legs,'rows':table},indent=1)+'\n')
    print('->',out/'comparison.json')


if __name__=='__main__':main()
