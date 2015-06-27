#!/usr/bin/python

import sys
import argparse
import pydot
import networkx as nx
import pylab as pl

import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description=\
        'Graph network topology graph from UART output')
parser.add_argument('graph_file', 
        help="file with graph in DOT format")
args = parser.parse_args()

fig = plt.figure()
ax = fig.add_subplot(1,1,1)

G_pd = pydot.graph_from_dot_file(args.graph_file)
G = nx.from_pydot(G_pd)
nx.draw(G)

plt.show()
