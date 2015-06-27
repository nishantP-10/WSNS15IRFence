/*	dijkstra.c

	Compute shortest paths in weighted graphs using Dijkstra's algorithm

	by: Steven Skiena
	date: March 6, 2002
*/

/*
Copyright 2003 by Steven S. Skiena; all rights reserved. 

Permission is granted for use in non-commerical applications
provided this copyright notice remains intact and unchanged.

This program appears in my book:

"Programming Challenges: The Programming Contest Training Manual"
by Steven Skiena and Miguel Revilla, Springer-Verlag, New York 2003.

See our website www.programming-challenges.com for additional information.

This book can be ordered from Amazon.com at

http://www.amazon.com/exec/obidos/ASIN/0387001638/thealgorithmrepo/

*/


#include <stdlib.h>
#include "wgraph.h"
#include "cfg.h"

#define MAXINT 0x7f

#define MAXV MAX_NODES

static bool intree[MAXV];		/* is the vertex in the tree yet? */
static int8_t distance[MAXV];		/* distance vertex is from start */

void dijkstra(graph *g, int8_t start, int8_t *parent)
{
	int8_t i;			/* counters */
	int8_t v;				/* current vertex to process */
	int8_t w;				/* candidate next vertex */
	int8_t weight;			/* edge weight */
	int8_t dist;			/* best current distance from start */

	for (i=0; i<g->nvertices; i++) {
		intree[i] = false;
		distance[i] = MAXINT;
		parent[i] = 0; /* zero is an invalid vertex */
	}

	distance[start] = 0;
	v = start;

	while (intree[v] == false) {
		intree[v] = true;
		for (i=0; i<g->degree[v]; i++) {
			w = g->edges[v][i].v;
			weight = g->edges[v][i].weight;
/* CHANGED */		if (distance[w] > (distance[v]+weight)) {
/* CHANGED */			distance[w] = distance[v]+weight;
/* CHANGED */			parent[w] = v;
			}
		}

		v = 0;
		dist = MAXINT;
		for (i=1; i<g->nvertices; i++)
			if ((intree[i] == false) && (dist > distance[i])) {
				dist = distance[i];
				v = i;
			}
	}
/*for (i=1; i<=g->nvertices; i++) OUTP("%d %d\n",i,distance[i]);*/
}

#if 0
main()
{
	graph g;
	int8_t i;

	read_graph(&g,false);
	dijkstra(&g,1);

        for (i=1; i<=g.nvertices; i++)
                find_path(1,i,parent);
        OUTP("\n");

}
#endif



