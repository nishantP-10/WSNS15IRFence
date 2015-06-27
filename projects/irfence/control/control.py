#!/usr/bin/python

import sys
import argparse
import pydot
import networkx as nx
import pylab as pl
import time
import threading as th
import select
import re
from bunch import Bunch

import matplotlib.pyplot as plt
import matplotlib.animation as anim


parser = argparse.ArgumentParser(description=\
        'Graph network topology graph from UART output')
parser.add_argument('dev', 
        help="UART device to scan for graph output")
parser.add_argument('-d', '--dedup', action='store_true',
        help="ignore duplicate graph info")
args = parser.parse_args()

CTRL_PREFIX = 'CTRL: '

G = None
prev_graph_str = None

fig = plt.figure()
ax = fig.add_subplot(1,1,1, axisbg='black')
ax.xaxis.set_visible(False)
ax.yaxis.set_visible(False)

x_lim = None
y_lim = None

def sync_graphs(G_old, G_new):
    added_nodes = set(G_new.nodes()) - set(G_old.nodes())
    for node in added_nodes:
        G_old.add_node(node)

    added_edges = set(G_new.edges()) - set(G_old.edges())
    for u, v in added_edges:
        G_old.add_edge(u, v)

def update_rf(G_old, G_new):
    sync_graphs(G_old, G_new)

    for u, v in G_old.edges():
        G_old[u][v]['rflink'] = False

    for u, v in G_new.edges():
        G_old[u][v]['rflink'] = True

def update_ir(G_old, G_new):
    sync_graphs(G_old, G_new)

    for u, v in G_old.edges():
        G_old[u][v]['irlink'] = False

    for u, v in G_new.edges():
        G_old[u][v]['irlink'] = True

def update_loc(G_old, G_new):
    sync_graphs(G_old, G_new)

    old_attrs = G_old.graph['graph']
    new_attrs = G_new.graph['graph']
    old_attrs['dim_x'] = new_attrs['dim_x']
    old_attrs['dim_y'] = new_attrs['dim_y']

    for node, data in G_old.nodes(data=True):
        if 'x' in data:
            del data['x']
        if 'y' in data:
            del data['y']

    for node, data in G_new.nodes(data=True):
        if 'x' in G_new.node[node]:
            G_old.node[node]['x'] = G_new.node[node]['x']
            G_old.node[node]['y'] = G_new.node[node]['y']
        else:
            print "WARN: new node with no coords: " + str(node)

updaters = {
    'RF': update_rf,
    'G': update_rf, # TODO: remove workaround
    'IR': update_ir,
    'LOC': update_loc,
}

# rflink, irlink, beam, breached
color_legend = {

    False: {
        False: {
            False: {
                False: None,
                True: None, # error
            },
            True: { # error
                False: None,
                True: None,
            }
        },
        True: {
            False: {
                False: '#0000ff',
                True: None, # error
            },
            True: {
                False: '#00ff00',
                True: '#ff0000',
            }
        }
    },

    True: {
        False: {
            False: {
                False: 'gray',
                True: None, # error
            },
            True: { # error
                False: None,
                True: None,
            }
        },
        True: {
            False: {
                False: '#0000ff',
                True: None, # error
            },
            True: {
                False: '#00ff00',
                True: '#ff0000',
            }
        }
    }
}

edge_specs = {
    'rflink': {
        'color': 'gray',
        'alpha': 0.5,
        'width': 8.0,
        'style': 'dotted',
    },
    'irlink': {
        'color': 'blue',
        'alpha': 0.75,
        'width': 2.0,
        'style': 'solid',
    },
    'beam': {
        'color': 'green',
        'alpha': 0.75,
        'width': 2.0,
        'style': 'solid',
    },
    'breached': {
        'color': 'red',
        'alpha': 0.75,
        'width': 2.0,
        'style': 'solid',
    },
}


def bflag(d, key):
    if key in d:
        return d[key]
    return False

def draw_graph(G, ax):
    ax.clear()

    # First try to get node locations from attributes
    unlocated_nodes = []
    located_nodes = []
    located_pos = {}
    unlocated_pos = {}
    for node, data in G.nodes(data=True):
        if 'x' in data and 'y' in data:
            located_nodes.append(node)
            located_pos[node] = (float(data['x']), float(data['y']))
            print "pos[", node, "]=", located_pos[node]
        else:
            unlocated_nodes.append(node)

    graph_attrs = G.graph['graph'] # the documented way does not work
    if 'dim_x' in graph_attrs and 'dim_y' in graph_attrs:
        d_x = float(graph_attrs['dim_x'])
        d_y = float(graph_attrs['dim_y'])
        x_lim = (-d_x, d_x)
        y_lim = (-d_y, d_y)
    else:
        x_lim = None
        y_lim = None

    if len(located_nodes) > 0:
        print "set axes: ", x_lim, y_lim
        ax.set_xlim(x_lim)
        ax.set_ylim(y_lim)

    # If no location attributes, fall back to a predefined layout
    if len(located_nodes) == 0:
        unlocated_pos = nx.circular_layout(G)
    elif len(unlocated_nodes) > 0: # some located and some unlocated nodes
        # arrange unlocated nodes in a row in the vertical middle
        x_span = float(abs(x_lim[0]) + abs(x_lim[1]))
        x_step = x_span / (len(unlocated_nodes) + 2)

        y = float(y_lim[0] + y_lim[1]) / 2
        x = y_lim[0] + x_step
        for node in unlocated_nodes:
            unlocated_pos[node] = (x, y)
            x += x_step

        #G_unlocated = nx.Graph()
        #G_unlocated.add_nodes_from(unlocated_nodes)
        #pos = nx.circular_layout(G_unlocated)

    NODE_SIZE = 600
    nx.draw_networkx_nodes(G, ax=ax, pos=located_pos,
                           nodelist=located_nodes,
                           node_size=NODE_SIZE, node_color='#00ffcc')
    nx.draw_networkx_nodes(G, ax=ax, pos=unlocated_pos,
                           nodelist=unlocated_nodes,
                           node_size=NODE_SIZE, node_color='#c8c8c8')

    pos = located_pos.copy()
    pos.update(unlocated_pos)
    nx.draw_networkx_labels(G, ax=ax, pos=pos,
                            font_size=16, font_weight='bold',
                            font_color='black')

    for prop in ['rflink', 'irlink', 'beam', 'breached']:

        for u, v in G.edges():

            e = G[u][v]

            #f = bflag
            #spec = color_legend[f(e,'rflink')][f(e,'irlink')][f(e,'beam')][f(e,'breached')]
            if not bflag(e, prop):
                continue

            spec = edge_specs[prop]
            nx.draw_networkx_edges(G, ax=ax, pos=pos, edgelist=[(u, v)],
                                   edge_color=spec['color'],
                                   alpha=spec['alpha'],
                                   width=spec['width'],
                                   style=spec['style'])

    plt.draw()

def handle_msg(line):
    global prev_graph_str
    global G

    # TODO: remove prefix workaround
    m = re.match(r'(?P<prefix>RF: )?digraph', line)
    if m:
        # TODO: remove prefix workaround
        if m.group('prefix') is not None and len(m.group('prefix')) > 0:
            graph_str = line[len(m.group('prefix')):]
        else:
            graph_str = line
        if not args.dedup or graph_str != prev_graph_str:
            print "parser: new graph: ", graph_str

            try:
                G_pd = pydot.graph_from_dot_data(graph_str)
                G_new = nx.from_pydot(G_pd)
            except Exception as e:
                print "Exception while parsing graph str: " + str(e)
                return

            if G is None:
                G = G_new

            updaters[G_new.name](G, G_new)

            draw_graph(G, ax)
            prev_graph_str = graph_str
        return

    m = re.match(r'fence: (?P<posts>.*)', line)
    if m:

        if not G:
            print "error: fence section, before graph"
            return

        nodes = []
        section_status = []
        if len(m.group('posts')) > 0:
            posts = m.group('posts').split()
        else:
            posts = []

        print "posts=", posts
        for post in posts:
            node, status = post.split(':')
            nodes.append(node)
            section_status.append(int(status))

        print "nodes=", nodes

        STATUS_NONE = 0
        STATUS_ACTIVE = 1
        STATUS_BREACHED = 2

        sections = []
        for i, node in enumerate(nodes):
            if i < len(nodes) - 1:
                sections.append(((nodes[i], nodes[i + 1]),
                                section_status[i]))

        print sections

        for u, v in G.edges():
            G[u][v]['beam'] = False
            G[u][v]['breached'] = False

        for node_pair, status in sections:
            out_node, in_node = node_pair
            
            edge = G.edge[out_node][in_node]
            print "edge=", edge
            print "out,in=", out_node, in_node

            print "status=", status
            if status == STATUS_NONE:
                edge['beam'] = False
            elif status == STATUS_ACTIVE:
                print "active"
                edge['beam'] = True
            elif status == STATUS_BREACHED:
                edge['breached'] = True
            print "edge=", edge

        draw_graph(G, ax)
        return

    print "parser: error: unexpected msg: '" + line + "'"

def parse(args):
    print "parser: started"

    fin = open(args.dev)

    graph_str = None
    prev_graph_str = None
    line = None
    while True:
        while fin in select.select([fin], [], [], 1)[0]:
            line = fin.readline()
            if line and line.startswith(CTRL_PREFIX):
                #try:
                handle_msg(line[len(CTRL_PREFIX):])
                #except Exception as e:
                #    print e
                #    print "failed to parse line: '" + line + '"'
                    
            else: # EOF
                time.sleep(0.010)
                break

parser = th.Thread(name="parser", target=parse, args=[args]);
parser.daemon = True # exit program when plt.show() returns

parser.start()

plt.show()
