#!/usr/bin/python

import pylab as pl
import pandas as pd
import re
import argparse

parser = argparse.ArgumentParser(description=\
        'Convert context switch trace data into per-task trace in CSV format')
parser.add_argument('trace_file',
        help='Raw trace from UART with ctx switch events')
parser.add_argument('output_file_prefix',
        help='Output file for time and task state in CSV')
args = parser.parse_args()

completion_re = r'\s*(?P<time_sec>\d+)\s*(?P<time_nsec>\d+)\s*' + \
                r'(?P<task_out>\d+)->(?P<task_in>\d+)'
version_re  = r'Nano-RK Version'

output_files = {}

for line in open(args.trace_file):
    m = re.match(completion_re, line)
    if not m:
        if re.match(version_re, line):
            continue
        else:
            raise Exception("Failed to parse line: '" + line + "'")

    task_in = int(m.group('task_in'))
    task_out = int(m.group('task_out'))

    for task in [task_in, task_out]:
        if task not in output_files:
            fout = open(args.output_file_prefix + "-task-" + str(task) + '.csv', "w")
            fout.write("time_s,is_running\n")
            output_files[task] = fout

    time = float(m.group('time_sec')) + float(m.group('time_nsec'))/1000000000

    output_files[task_out].write("%f,0\n" % (time))
    output_files[task_in].write("%f,1\n" % (time))

for task in output_files:
    output_files[task].close()
