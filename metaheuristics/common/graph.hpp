/*
 * Grafo nao direcionado e somente para leitura, armazenado em CSR.
 * Os vertices sao numerados de 0 a n-1 e as listas de vizinhos ficam
 * ordenadas.
 *
 * Uso tipico:
 *   graph *g = graph_load_dimacs("instancia.col");
 *   uint32_t degree;
 *   const uint32_t *nbrs = graph_neighbors(g, v, &degree);
 *   for (uint32_t i = 0; i < degree; i++) { ... nbrs[i] ... }
 *   graph_free(g);
 *
 * O formato binario evita analisar o arquivo DIMACS novamente em execucoes
 * posteriores:
 *
 *   graph *g = graph_load_dimacs("instancia.col");
 *   graph_save_binary(g, "instancia.bin");
 *   graph_free(g);
 *   ...
 *   graph *g2 = graph_load_binary("instancia.bin");
 */
#ifndef GRAPH_H
#define GRAPH_H

#include <stddef.h>
#include <stdint.h>

typedef struct graph
{
	uint32_t n; /* numero de vertices (0..n-1) */
	uint64_t m; /* numero de arestas (grafo nao-direcionado) */

	/* Os vizinhos de v ocupam adjacency[offset[v] .. offset[v + 1] - 1]. */
	uint32_t *offset; /* tamanho n+1 */
	uint32_t *adjacency; /* tamanho 2*m */

	/* Preenchidos quando o grafo foi carregado do formato binario. */
	void *mmap_base;
	size_t mmap_len;
} graph;

/**
 * @brief Carrega um grafo no formato DIMACS .col.
 * @param path Caminho do arquivo.
 * @return Grafo carregado, ou NULL em caso de erro.
 */
graph *graph_load_dimacs(const char *path);

/**
 * @brief Salva um grafo no formato binario interno.
 * @param g Grafo que sera salvo.
 * @param path Caminho do arquivo de destino.
 * @return 0 em caso de sucesso ou -1 em caso de erro.
 */
int graph_save_binary(const graph *g, const char *path);

/**
 * @brief Carrega um grafo no formato binario interno.
 * @param path Caminho do arquivo.
 * @return Grafo carregado, ou NULL em caso de erro.
 */
graph *graph_load_binary(const char *path);

/**
 * @brief Libera um grafo.
 * @param g Grafo a liberar. Pode ser NULL.
 */
void graph_free(graph *g);

/* Consultas basicas. */

/** @brief Retorna o numero de vertices. */
static inline uint32_t graph_num_vertices(const graph *g)
{
	return g->n;
}

/** @brief Retorna o numero de arestas. */
static inline uint64_t graph_num_edges(const graph *g)
{
	return g->m;
}

/** @brief Retorna o grau de um vertice. */
static inline uint32_t graph_degree(const graph *g, uint32_t v)
{
	return g->offset[v + 1] - g->offset[v];
}

/**
 * @brief Retorna a lista de vizinhos de um vertice.
 * @param g Grafo consultado.
 * @param v Vertice consultado.
 * @param out_degree Recebe o numero de vizinhos.
 * @return Ponteiro valido enquanto o grafo existir.
 */
static inline const uint32_t *graph_neighbors(const graph *g, uint32_t v, uint32_t *out_degree)
{
	uint32_t start = g->offset[v];
	*out_degree = g->offset[v + 1] - start;
	return &g->adjacency[start];
}

/**
 * @brief Verifica se existe uma aresta entre dois vertices.
 * @param g Grafo consultado.
 * @param u Primeiro vertice.
 * @param v Segundo vertice.
 * @return Valor diferente de zero quando a aresta existe.
 */
int graph_has_edge(const graph *g, uint32_t u, uint32_t v);

#endif /* GRAPH_H */
