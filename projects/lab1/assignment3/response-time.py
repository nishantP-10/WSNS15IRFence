#!/usr/bin/python

import pylab as pl
import pandas as pd
import re
import argparse

parser = argparse.ArgumentParser(description=\
        'Calculate job response time statistics')
parser.add_argument('job_trace_file',
        help='Job release and completion times in CSV format')
args = parser.parse_args()

job_data = pd.read_csv(args.job_trace_file)
job_data['response_s'] = job_data['completion_s'] - job_data['release_s']
job_response_data = job_data[['task', 'response_s']]
response_by_task = job_response_data.groupby('task')
response_stat = response_by_task.describe()
print response_stat[['response_s']]

