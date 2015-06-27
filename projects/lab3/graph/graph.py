#!/usr/bin/python

import sys
import argparse
import pydot
import networkx as nx
import pylab as pl

import matplotlib.pyplot as plt
import matplotlib.animation as anim


parser = argparse.ArgumentParser(description=\
        'Graph network topology graph from UART output')
parser.add_argument('dev', 
        help="UART device to scan for graph output")
args = parser.parse_args()

TEMP_FILE = '/tmp/temp-graph.dot'

fin = open(args.dev)
fout = None
last_graph = ''

fig = plt.figure()
ax = fig.add_subplot(1,1,1)

def update(i):
    global fout
    global fin
    global last_graph

    line = fin.readline()
    if line.startswith('graph G'):
        fout = open(TEMP_FILE, "w")
    if fout is not None:
        fout.write(line + '\n')
        print line,
    if line.startswith('}'):
        fout.close()
        fout = None

        fin_new = open(TEMP_FILE)
        new_graph = fin_new.read()
        if last_graph != new_graph:
            last_graph = new_graph
            G_pd = pydot.graph_from_dot_file(TEMP_FILE)
            G = nx.from_pydot(G_pd)
            ax.clear()
            nx.draw(G)
        fin_new.close()

a = anim.FuncAnimation(fig, update, repeat=True)
plt.show()
