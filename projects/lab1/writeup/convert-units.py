#!/usr/bin/python

import pylab as pl
import pandas as pd
import argparse

parser = argparse.ArgumentParser(description=\
        'Convert RSSI to dB and distance to meters')
parser.add_argument('in_data_file',
        help='Raw RSSI data file in CSV format')
parser.add_argument('out_data_file',
        help='Converted RSSI data file in CSV format')
args = parser.parse_args()

raw_data = pd.read_csv(args.in_data_file)

RSSI_STEP_DB = 5 # matches the value in the display_rssi function on the node

dist_m = raw_data['dist_ft'].mul(0.3048) # ft to m
rssi_db = raw_data['rssi_bin'].map(lambda x: int(str(x), 2)) # binary to dec
rssi_db = rssi_db.mul(RSSI_STEP_DB) # from binned to dB

conv_data = pd.DataFrame(dict(dist_m=dist_m, rssi_db=rssi_db))
conv_data.to_csv(args.out_data_file, index=False)
