#!/usr/bin/python

import pylab as pl
import numpy as np
import pandas as pd
import argparse

from math import log

parser = argparse.ArgumentParser(description=\
        'Calculate path loss exponent from RSSI vs distance')
parser.add_argument('rssi_data_file',
        help='RSSI (dB) vs dist (m) data file in CSV format')
parser.add_argument('-f', '--figure',
        help='path to file to save figure to')
parser.add_argument('-s', '--size', nargs=2, metavar=('WIDTH', 'HEIGHT'),
        type=float, default=(6, 4),
        help='size of the figure (in)')
args = parser.parse_args()

data = pd.read_csv(args.rssi_data_file)
rssi_data = data['rssi_db']
dist_data = data['dist_m']

# Solve over-determined system of equations for unknowns n, C
# L_i = 10 * n * log10 d_i + C

A_n = dist_data.map(lambda x: 10.0*log(x, 10))
A_C = np.ones(len(dist_data))
A = np.matrix([A_n, A_C]).T
b = rssi_data.mul(-1) # RSSI = -L (additive inverse of path loss)
sol = np.linalg.lstsq(A, b)
n, C = sol[0]

print "n =", n, "C =", C

pl.figure()
pl.plot(dist_data, dist_data * n + C, label='least-squares sol.', color='k')
pl.scatter(dist_data, b, label='measured L (-RSSI)', marker='o', color='k')
pl.title("Least-squares residuals for path loss system of equations")
pl.xlabel("Distance (m)")
pl.ylabel("Path loss (dB)")
pl.legend(loc=0)
pl.grid()

if args.figure:
    pl.savefig(args.figure, figsize=args.size, bbox_inches='tight')
else:
    pl.show()
