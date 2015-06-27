#!/usr/bin/python

import pylab as pl
import pandas as pd
import numpy as np
import re
import argparse

from matplotlib.ticker import MultipleLocator

parser = argparse.ArgumentParser(description=\
        'Plot context switch graph')
parser.add_argument('exec_trace_file', nargs='+', 
        help='Execution traces, one per task')
parser.add_argument('--duration', type=float, default=-1,
        help='Limit duration of the plot (seconds)')
parser.add_argument('-f', '--figure',
        help='path to file to save figure to')
parser.add_argument('-s', '--size', nargs=2, metavar=('WIDTH', 'HEIGHT'),
        type=float, default=(6, 4),
        help='size of the figure (in)')
args = parser.parse_args()

Y_MARGIN = 0.05 # fraction
Y_MARGIN_EXTRA = 0.05 # absolute value
X_MARGIN = 0.05 # fraction

fig, ax_list = pl.subplots(len(args.exec_trace_file), sharex=True)

for i, task_trace_file in enumerate(args.exec_trace_file):
    ax = ax_list[i]
    trace_data = pd.read_csv(task_trace_file)

    if args.duration >= 0:
        trace_data = trace_data[trace_data['time_s'] <= args.duration]

    time_data = trace_data['time_s']
    state_data = trace_data['is_running']

    ax.step(time_data, state_data, where='post')

    ax.set_xlim([time_data.min() * (1.0 - X_MARGIN),
                 time_data.max() * (1.0 + X_MARGIN)])
    ax.set_ylim([state_data.min() * (1.0 - Y_MARGIN) - Y_MARGIN_EXTRA,
                 state_data.max() * (1.0 + Y_MARGIN)])

    tick_locator = MultipleLocator(1)
    ax.xaxis.set_major_locator(tick_locator)

    ax.set_title('Execution state of Task ' + str(i))
    if i == len(args.exec_trace_file) - 1:
        ax.set_xlabel('Time (s)')
    ax.set_ylabel('State (1.0 = running)')

if args.figure:
    pl.savefig(args.figure, figsize=args.size, bbox_inches='tight')
else:
    pl.show()
