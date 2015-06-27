#!/usr/bin/python

import pylab as pl
import pandas as pd
import numpy as np
import re
import argparse

parser = argparse.ArgumentParser(description=\
        'Construct context switch data frame')
parser.add_argument('job_trace_file',
        help='Job start and end times in CSV format')
parser.add_argument('output_file_prefix',
        help='Prefix to name output files (<prefix>-task-<idx>.csv)')
args = parser.parse_args()

job_data = pd.read_csv(args.job_trace_file)

TASKS = job_data['task'].unique()

for task in TASKS:
    task_data = job_data[job_data['task'] == task][['start_s', 'end_s']]

    start_times = pd.Series(np.array(task_data['start_s']))
    ones = pd.Series(np.ones(task_data['start_s'].count()))
    start_data = pd.concat([start_times, ones], axis=1)

    end_times = pd.Series(np.array(task_data['end_s']))
    zeroes = pd.Series(np.zeros(task_data['end_s'].count()))
    end_data = pd.concat([end_times, zeroes], axis=1)

    trace_data = pd.concat([start_data, end_data], axis=0)
    trace_data.columns = ['time_s', 'is_running']
    trace_data.sort('time_s', inplace=True)

    trace_data.to_csv(args.output_file_prefix + '-task-' + str(task) + '.csv',
                      index=False)
