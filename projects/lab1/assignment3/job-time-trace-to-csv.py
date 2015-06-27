#!/usr/bin/python

import pylab as pl
import pandas as pd
import re
import argparse

parser = argparse.ArgumentParser(description=\
        'Convert trace data with start and end times into CSV format')
parser.add_argument('trace_file',
        help='Raw trace from UART with task completion times')
parser.add_argument('output_file',
        help='Output file for job release and completion in CSV')
args = parser.parse_args()

# task idx -> period (seconds)
PERIODS_S = {
    1: 1.0,
    2: 2.0,
}

# Example trace lines
# Task2: 0 138671946
# Task1: 0 441406476

completion_re = r'Task(?P<task>\d+):' + \
                r'\s*(?P<start_sec>\d+)\s*(?P<start_nsec>\d+)' + \
                r'\s*(?P<end_sec>\d+)\s*(?P<end_nsec>\d+)'
version_re  = r'Nano-RK Version'

fout = open(args.output_file, "w")
fout.write("task,start_s,end_s\n")

for line in open(args.trace_file):
    m = re.match(completion_re, line)
    if not m:
        if re.match(version_re, line):
            continue
        else:
            raise Exception("Failed to parse line: '" + line + "'")
    
    task = int(m.group('task'))
    start = float(m.group('start_sec')) + float(m.group('start_nsec'))/1000000000
    end = float(m.group('end_sec')) + float(m.group('end_nsec'))/1000000000

    fout.write("%d,%f,%f\n" % (task, start, end))

fout.close()
