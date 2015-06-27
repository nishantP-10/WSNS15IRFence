#!/usr/bin/python

import pylab as pl
import pandas as pd
import argparse

parser = argparse.ArgumentParser(description='Plot RSSI vs distance')
parser.add_argument('data_file',
        help='RSSI data file (m, dB units) in CSV format')
parser.add_argument('-f', '--figure',
        help='path to file to save figure to')
parser.add_argument('-s', '--size', nargs=2, metavar=('WIDTH', 'HEIGHT'),
        type=float, default=(6, 4),
        help='size of the figure (in)')
args = parser.parse_args()

data = pd.read_csv(args.data_file)

pl.figure()
pl.plot(data['dist_m'], data['rssi_db'], marker='o', color='k')

pl.title('RSSI as a function of separation distance')
pl.xlabel('Distance (m)')
pl.ylabel('RSSI (dB)')
pl.grid()

if args.figure:
    pl.savefig(args.figure, figsize=args.size, bbox_inches='tight')
else:
    pl.show()
