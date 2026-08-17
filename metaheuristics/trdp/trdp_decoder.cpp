#include "trdp_decoder.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

TDRPDecoder::TDRPDecoder(const graph *graph)
    : graph_(graph)
    , evaluation_counter(std::make_shared<std::atomic<uint64_t>>(0))
{
	if (graph_ == nullptr)
	{
		throw std::invalid_argument("TDRPDecoder: O grafo passado é nullptr.");
	}
}

// verifica se o vértice segue as restrições do problema
static inline bool is_vertex_feasible(const graph *g, const std::vector<uint8_t> &f, uint32_t v)
{
	uint32_t deg_v;
	const uint32_t *neighbors_v = graph_neighbors(g, v, &deg_v);

	if (f[v] == 0)
	{
		// precisa de um vizinho com rótulo 2
		for (uint32_t i = 0; i < deg_v; ++i)
		{
			if (f[neighbors_v[i]] == 2)
			{
				return true;
			}
		}
		return false;
	}

	// precisa de um vizinho > 0
	for (uint32_t i = 0; i < deg_v; ++i)
	{
		if (f[neighbors_v[i]] > 0)
		{
			return true;
		}
	}
	return false;
}

static inline void reduce_weight_heuristic(const graph *g, std::vector<uint8_t> &f)
{
	const uint32_t n = graph_num_vertices(g);
	for (uint32_t u = 0; u < n; ++u)
	{
		// Impossível reduzir o rótulo
		if (f[u] == 0)
		{
			continue;
		}

		const uint8_t old_label = f[u];
		bool decrease_successful = false;

		// percorre os valores válidos
		for (uint8_t new_label = 0; new_label < old_label; ++new_label)
		{
			f[u] = new_label;
			bool feasible = is_vertex_feasible(g, f, u);

			// se o vértice continuar viável, verifica se os vizinhos também continuam
			if (feasible)
			{
				uint32_t deg_u;
				const uint32_t *neighbors_u = graph_neighbors(g, u, &deg_u);
				for (uint32_t i = 0; i < deg_u; ++i)
				{
					if (!is_vertex_feasible(g, f, neighbors_u[i]))
					{
						feasible = false;
						break;
					}
				}
			}

			if (feasible)
			{
				decrease_successful = true;
				break;
			}
		}

		if (!decrease_successful)
		{
			f[u] = old_label;
		}
	}
}

double TDRPDecoder::decode(const std::vector<double> &chromosome) const
{
	// conta uma nova avaliação da função objetivo
	evaluation_counter->fetch_add(1, std::memory_order_relaxed);

	const uint32_t n = graph_num_vertices(graph_);

	if (chromosome.size() != n)
	{
		throw std::invalid_argument(
		    "TDRPDecoder: O tamanho do cromossomos deve ser igual o número de vértices do grafo;");
	}

	std::vector<uint8_t> f(n, 0);

	// converte as chaves aleatórias no espaço do problema
	for (uint32_t i = 0; i < n; ++i)
	{
		f[i] = static_cast<uint8_t>(chromosome[i] * 3.0);
	}

	// garante que todo vértice de rôtulo 0 tenha vizinho 2
	for (uint32_t v = 0; v < n; ++v)
	{
		if (f[v] != 0)
		{
			continue;
		}

		// pega o grau de v e o inicio do vetor de seus vizinhos
		uint32_t deg_v;
		const uint32_t *neighbors_v = graph_neighbors(graph_, v, &deg_v);
		if (deg_v == 0)
		{
			// vértice isolado, para o trdp não pode existir vértices isolados, logo a instância não é válida
			continue;
		}

		// procura o vizinho com rôtulo 2
		bool has_neighbor_2 = false;
		uint32_t chosen = neighbors_v[0];
		for (uint32_t i = 0; i < deg_v; ++i)
		{
			const uint32_t u = neighbors_v[i];
			if (f[u] == 2)
			{
				// se ouver um vizinho com rôtulo 2, apenas continua
				has_neighbor_2 = true;
				continue;
			}

			//prioriza promover quem já tem o maior rôtulo e desempata pela chave
			if (f[u] > f[chosen] || (f[u] == f[chosen] && chromosome[u] > chromosome[chosen]))
			{
				chosen = u;
			}
		}

		if (has_neighbor_2)
		{
			continue;
		}

		f[chosen] = 2;
	}

	for (uint32_t v = 0; v < n; ++v)
	{
		if (f[v] == 0)
		{
			continue;
		}

		// pega o grau de v e o inicio do vetor de seus vizinhos
		uint32_t deg_v;
		const uint32_t *neighbors_v = graph_neighbors(graph_, v, &deg_v);
		if (deg_v == 0)
		{
			// vértice isolado, para o trdp não pode existir vértices isolados, logo a instância não é válida
			continue;
		}

		// procura o vizinho com rôtulo 2
		bool has_positive_neighbor = false;
		uint32_t chosen = neighbors_v[0];
		for (uint32_t i = 0; i < deg_v; ++i)
		{
			const uint32_t u = neighbors_v[i];
			if (f[u] > 0)
			{
				has_positive_neighbor = true;
				break;
			}

			// escolhe quais incrementar pela chave
			if (chromosome[u] > chromosome[chosen])
			{
				chosen = u;
			}
		}

		if (has_positive_neighbor)
		{
			continue;
		}
		f[chosen] = 1;
	}

	// roda a heurística de redução de pesos
	reduce_weight_heuristic(graph_, f);

	// retorno do fitness
	double fitness = 0.0;
	for (uint32_t v = 0; v < n; ++v)
	{
		fitness += f[v];
	}

	return fitness;
}
