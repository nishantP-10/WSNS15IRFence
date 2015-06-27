/*

Header file for weighted graph type

	by Steven Skiena
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

#include "cfg.h"

#define MAXV        MAX_NODES        /* maximum number of vertices */
#define MAXDEGREE   MAX_NODES        /* maximum outdegree of a vertex */

typedef struct {
	uint8_t v;				/* neighboring vertex */
	uint8_t weight;			/* edge weight */
} edge;

typedef struct {
	edge edges[MAXV+1][MAXDEGREE];	/* adjacency info */
	uint8_t degree[MAXV+1];		/* outdegree of each vertex */
	uint8_t nvertices;			/* number of vertices in the graph */
	uint8_t nedges;			/* number of edges in the graph */
} graph;


