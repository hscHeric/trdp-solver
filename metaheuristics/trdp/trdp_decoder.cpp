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

double TDRPDecoder::decode(const std::vector<double> &chromosome) const
{
	// Conta uma nova avaliação da função objetivo.
	evaluation_counter->fetch_add(1, std::memory_order_relaxed);

	const uint32_t n = graph_num_vertices(graph_);

	if (chromosome.size() != n)
	{
		throw std::invalid_argument(
		    "TDRPDecoder: O tamanho do cromossomos deve ser igual o número de vértices do grafo;");
	}

	double fitness = 0.0;

	std::vector<uint8_t> f(n, 0);
	for (uint32_t i = 0; i < n; ++i)
	{
		f[i] = static_cast<uint8_t>(chromosome[i] * 3.0);
	}

	for (uint32_t v = 0; v < n; ++v)
	{
		if (f[v] != 0)
		{
			continue;
		}

		uint32_t deg = graph_degree(graph_, v);
		const uint32_t *neighbors = graph_neighbors(graph_, v, &deg);
	}

	return fitness;
}
