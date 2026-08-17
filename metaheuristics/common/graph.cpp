
//#define _GNU_SOURCE // NOLINT

#include "graph.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* Indica ao compilador os caminhos mais comuns do codigo. */
#if defined(__GNUC__)

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define HOT __attribute__((hot))

#else

#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#define HOT

#endif

#define BIN_MAGIC 0x47524231u /* "GRB1" */
#define BIN_VERSION 1u

typedef struct
{
	uint32_t magic;
	uint32_t version;
	uint32_t n;
	uint32_t reserved; /* alinhamento; mantem o header em 8 bytes redondos */
	uint64_t m;
} BinHeader;

/* Leitura do arquivo DIMACS mapeado em memoria. */
static inline const char *skip_to_digit(const char *p, const char *end)
{
	while (p < end && (*p < '0' || *p > '9') && *p != '\n')
	{
		p++;
	}
	return p;
}

static inline const char *skip_line(const char *p, const char *end)
{
	while (p < end && *p != '\n')
	{
		p++;
	}
	return (p < end) ? p + 1 : p;
}

/* Le um inteiro sem sinal e avanca o ponteiro ate o fim do numero. */
static inline uint32_t read_uint(const char **p, const char *end)
{
	const char *s = *p;
	uint32_t v = 0;
	while (s < end && *s >= '0' && *s <= '9')
	{
		v = v * 10u + (uint32_t)(*s - '0');
		s++;
	}
	*p = s;
	return v;
}

/* Construcao da representacao CSR. */

typedef struct
{
	uint32_t u, v;
} Edge;

static int compare_uint32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a;
	uint32_t y = *(const uint32_t *)b;
	return (x > y) - (x < y);
}

/* Monta os vetores CSR e ordena a vizinhanca de cada vertice. */
static int build_csr(graph *g, const Edge *edges, uint64_t m, uint32_t n)
{
	g->n = n;
	g->m = m;

	g->offset = (uint32_t *)calloc((size_t)n + 1, sizeof(uint32_t));
	if (UNLIKELY(!g->offset))
	{
		return -1;
	}

	/* Cada aresta aparece na lista de seus dois extremos. */
	for (uint64_t i = 0; i < m; i++)
	{
		g->offset[edges[i].u + 1]++;
		g->offset[edges[i].v + 1]++;
	}
	/* Converte os graus em posicoes iniciais no vetor de adjacencia. */
	for (uint32_t v = 0; v < n; v++)
	{
		g->offset[v + 1] += g->offset[v];
	}

	uint64_t total_adjacency = g->offset[n];
	g->adjacency = (uint32_t *)malloc(total_adjacency * sizeof(uint32_t));
	if (UNLIKELY(!g->adjacency))
	{
		free(g->offset);
		g->offset = NULL;
		return -1;
	}

	/* Mantem a proxima posicao livre de cada vertice. */
	uint32_t *cursor = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
	if (UNLIKELY(!cursor))
	{
		free(g->offset);
		free(g->adjacency);
		g->offset = NULL;
		g->adjacency = NULL;
		return -1;
	}
	memcpy(cursor, g->offset, (size_t)n * sizeof(uint32_t));

	for (uint64_t i = 0; i < m; i++)
	{
		uint32_t u = edges[i].u, v = edges[i].v;
		g->adjacency[cursor[u]++] = v;
		g->adjacency[cursor[v]++] = u;
	}
	free(cursor);

	/* A ordenacao permite procurar arestas por busca binaria. */
	for (uint32_t v = 0; v < n; v++)
	{
		uint32_t degree = g->offset[v + 1] - g->offset[v];
		if (degree > 1)
		{
			qsort(&g->adjacency[g->offset[v]], degree, sizeof(uint32_t), compare_uint32);
		}
	}

	return 0;
}

/* Carregamento do formato DIMACS .col. */

HOT graph *graph_load_dimacs(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (UNLIKELY(fd < 0))
	{
		fprintf(stderr, "graph_load_dimacs: nao foi possivel abrir '%s': %s\n", path, strerror(errno));
		return NULL;
	}

	struct stat st;
	if (UNLIKELY(fstat(fd, &st) != 0 || st.st_size <= 0))
	{
		fprintf(stderr, "graph_load_dimacs: falha ao obter tamanho de '%s'\n", path);
		close(fd);
		return NULL;
	}
	size_t file_size = (size_t)st.st_size;

	void *map = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (UNLIKELY(map == MAP_FAILED))
	{
		fprintf(stderr, "graph_load_dimacs: mmap falhou para '%s': %s\n", path, strerror(errno));
		return NULL;
	}
	madvise(map, file_size, MADV_SEQUENTIAL);

	const char *p = (const char *)map;
	const char *end = p + file_size;

	uint32_t n = 0;
	uint32_t m_declared = 0;
	int header_found = 0;

	/* O vetor cresce se o cabecalho declarar menos arestas que o arquivo. */
	uint64_t capacity = 1024;
	uint64_t total_edges = 0;
	Edge *edges = (Edge *)malloc(capacity * sizeof(Edge));
	if (UNLIKELY(!edges))
	{
		munmap(map, file_size);
		fprintf(stderr, "graph_load_dimacs: falha de alocacao\n");
		return NULL;
	}

	while (p < end)
	{
		/* Ignora espacos e linhas vazias. */
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
		{
			p++;
		}
		if (p >= end)
		{
			break;
		}

		char kind = *p;
		if (kind == 'c')
		{
			p = skip_line(p, end);
			continue;
		}
		if (kind == 'p')
		{
			p = skip_to_digit(p, end);
			n = read_uint(&p, end);
			p = skip_to_digit(p, end);
			m_declared = read_uint(&p, end);
			header_found = 1;
			if (m_declared > 0)
			{
				capacity = m_declared;
				Edge *grown = (Edge *)realloc(edges, capacity * sizeof(Edge));
				if (UNLIKELY(!grown))
				{
					free(edges);
					munmap(map, file_size);
					fprintf(stderr, "graph_load_dimacs: falha de alocacao\n");
					return NULL;
				}
				edges = grown;
			}
			p = skip_line(p, end);
			continue;
		}
		if (kind == 'e')
		{
			p++;
			p = skip_to_digit(p, end);
			uint32_t u1 = read_uint(&p, end);
			p = skip_to_digit(p, end);
			uint32_t v1 = read_uint(&p, end);

			if (UNLIKELY(u1 == 0 || v1 == 0))
			{
				/* Vertices DIMACS comecam em 1. */
				p = skip_line(p, end);
				continue;
			}
			uint32_t u = u1 - 1, v = v1 - 1;

			if (UNLIKELY(total_edges >= capacity))
			{
				capacity = capacity * 2 + 16;
				Edge *grown = (Edge *)realloc(edges, capacity * sizeof(Edge));
				if (UNLIKELY(!grown))
				{
					free(edges);
					munmap(map, file_size);
					fprintf(stderr, "graph_load_dimacs: falha de alocacao\n");
					return NULL;
				}
				edges = grown;
			}
			edges[total_edges].u = u;
			edges[total_edges].v = v;
			total_edges++;

			p = skip_line(p, end);
			continue;
		}
		/* Linhas desconhecidas sao ignoradas. */
		p = skip_line(p, end);
	}

	munmap(map, file_size);

	if (UNLIKELY(!header_found))
	{
		free(edges);
		fprintf(stderr, "graph_load_dimacs: '%s' sem linha de cabecalho 'p edge n m'\n", path);
		return NULL;
	}

	/* Aceita arquivos cujo maior vertice ultrapasse o valor do cabecalho. */
	uint32_t max_index = 0;
	for (uint64_t i = 0; i < total_edges; i++)
	{
		if (edges[i].u + 1 > max_index)
			max_index = edges[i].u + 1;
		if (edges[i].v + 1 > max_index)
			max_index = edges[i].v + 1;
	}
	if (max_index > n)
	{
		n = max_index;
	}

	graph *g = (graph *)calloc(1, sizeof(graph));
	if (UNLIKELY(!g))
	{
		free(edges);
		fprintf(stderr, "graph_load_dimacs: falha de alocacao\n");
		return NULL;
	}

	if (UNLIKELY(build_csr(g, edges, total_edges, n) != 0))
	{
		free(edges);
		free(g);
		fprintf(stderr, "graph_load_dimacs: falha ao construir CSR\n");
		return NULL;
	}

	free(edges);
	return g;
}

/* Gravacao do formato binario interno. */

int graph_save_binary(const graph *g, const char *path)
{
	FILE *f = fopen(path, "wb");
	if (UNLIKELY(!f))
	{
		fprintf(stderr, "graph_save_binary: nao foi possivel criar '%s': %s\n", path, strerror(errno));
		return -1;
	}

	BinHeader header;
	header.magic = BIN_MAGIC;
	header.version = BIN_VERSION;
	header.n = g->n;
	header.reserved = 0;
	header.m = g->m;

	size_t ok = 1;
	ok &= fwrite(&header, sizeof(header), 1, f) == 1;
	ok &= fwrite(g->offset, sizeof(uint32_t), (size_t)g->n + 1, f) == (size_t)g->n + 1;
	ok &= fwrite(g->adjacency, sizeof(uint32_t), (size_t)(2 * g->m), f) == (size_t)(2 * g->m);

	fclose(f);
	if (UNLIKELY(!ok))
	{
		fprintf(stderr, "graph_save_binary: falha ao escrever '%s'\n", path);
		remove(path);
		return -1;
	}
	return 0;
}

/* Carregamento do formato binario interno. */

HOT graph *graph_load_binary(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (UNLIKELY(fd < 0))
	{
		fprintf(stderr, "graph_load_binary: nao foi possivel abrir '%s': %s\n", path, strerror(errno));
		return NULL;
	}

	struct stat st;
	if (UNLIKELY(fstat(fd, &st) != 0 || (size_t)st.st_size < sizeof(BinHeader)))
	{
		fprintf(stderr, "graph_load_binary: '%s' invalido ou truncado\n", path);
		close(fd);
		return NULL;
	}
	size_t file_size = (size_t)st.st_size;

	void *map = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (UNLIKELY(map == MAP_FAILED))
	{
		fprintf(stderr, "graph_load_binary: mmap falhou para '%s': %s\n", path, strerror(errno));
		return NULL;
	}
	madvise(map, file_size, MADV_RANDOM);

	const BinHeader *header = (const BinHeader *)map;
	if (UNLIKELY(header->magic != BIN_MAGIC || header->version != BIN_VERSION))
	{
		fprintf(stderr, "graph_load_binary: '%s' com magic/versao incompativel\n", path);
		munmap(map, file_size);
		return NULL;
	}

	size_t expected_size =
	    sizeof(BinHeader) + ((size_t)header->n + 1) * sizeof(uint32_t) + (size_t)(2 * header->m) * sizeof(uint32_t);
	if (UNLIKELY(expected_size != file_size))
	{
		fprintf(stderr,
		        "graph_load_binary: '%s' com tamanho inconsistente "
		        "(esperado %zu, encontrado %zu)\n",
		        path, expected_size, file_size);
		munmap(map, file_size);
		return NULL;
	}

	graph *g = (graph *)calloc(1, sizeof(graph));
	if (UNLIKELY(!g))
	{
		munmap(map, file_size);
		return NULL;
	}

	g->n = header->n;
	g->m = header->m;
	g->mmap_base = map;
	g->mmap_len = file_size;

	const char *after_header = (const char *)map + sizeof(BinHeader);
	g->offset = (uint32_t *)after_header;
	g->adjacency = (uint32_t *)(after_header + ((size_t)g->n + 1) * sizeof(uint32_t));

	return g;
}

void graph_free(graph *g)
{
	if (!g)
	{
		return;
	}
	if (g->mmap_base)
	{
		munmap(g->mmap_base, g->mmap_len);
		/* offset e adjacency fazem parte do mapeamento. */
	}
	else
	{
		free(g->offset);
		free(g->adjacency);
	}
	free(g);
}

int graph_has_edge(const graph *g, uint32_t u, uint32_t v)
{
	uint32_t degree_u, degree_v;
	const uint32_t *nbrs_u = graph_neighbors(g, u, &degree_u);
	const uint32_t *nbrs_v = graph_neighbors(g, v, &degree_v);

	/* Usa a menor lista de vizinhos. */
	const uint32_t *nbrs = nbrs_u;
	uint32_t degree = degree_u;
	uint32_t target = v;
	if (degree_v < degree_u)
	{
		nbrs = nbrs_v;
		degree = degree_v;
		target = u;
	}

	uint32_t lo = 0, hi = degree;
	while (lo < hi)
	{
		uint32_t mid = lo + (hi - lo) / 2;
		if (nbrs[mid] < target)
		{
			lo = mid + 1;
		}
		else
		{
			hi = mid;
		}
	}
	return (lo < degree && nbrs[lo] == target);
}
