#include "common/graph.hpp"
#include "external/brkga_api/brkga.hpp"
#include "trdp/trdp_decoder.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <thread>

namespace
{
constexpr unsigned POPULATION_SIZE = 100;
constexpr double ELITE_PERCENTAGE = 0.20;
constexpr double MUTANT_PERCENTAGE = 0.10;
constexpr double ELITE_INHERITANCE = 0.70;
constexpr unsigned NUM_POPULATIONS = 3;
constexpr unsigned NUM_GENERATIONS = 1000;
constexpr unsigned MIGRATION_INTERVAL = 100;
constexpr unsigned NUM_MIGRANTS = 2;
constexpr unsigned RNG_SEED = 0;
} // namespace

int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		std::cerr << "Uso: " << argv[0] << " <grafo.col>\n";
		return 1;
	}

	std::unique_ptr<graph, decltype(&graph_free)> input_graph(graph_load_dimacs(argv[1]), graph_free);
	if (!input_graph)
	{
		return 1;
	}

	try
	{
		const unsigned num_threads = std::max(1u, std::thread::hardware_concurrency());
		TDRPDecoder decoder(input_graph.get());
		std::mt19937 rng(RNG_SEED);

		BRKGA<TDRPDecoder> algorithm(
		    graph_num_vertices(input_graph.get()),
		    POPULATION_SIZE,
		    ELITE_PERCENTAGE,
		    MUTANT_PERCENTAGE,
		    ELITE_INHERITANCE,
		    decoder,
		    rng,
		    NUM_POPULATIONS,
		    num_threads);

		const auto start = std::chrono::steady_clock::now();
		for (unsigned generation = 0; generation < NUM_GENERATIONS; generation += MIGRATION_INTERVAL)
		{
			const unsigned block_size = std::min(MIGRATION_INTERVAL, NUM_GENERATIONS - generation);
			algorithm.evolve(block_size);

			if (generation + block_size < NUM_GENERATIONS)
			{
				algorithm.exchangeElite(NUM_MIGRANTS);
			}
		}
		const auto end = std::chrono::steady_clock::now();

		const std::chrono::duration<double> elapsed = end - start;
		std::cout << "Vertices: " << graph_num_vertices(input_graph.get()) << '\n'
		          << "Arestas: " << graph_num_edges(input_graph.get()) << '\n'
		          << "Melhor fitness: " << algorithm.getBestFitness() << '\n'
		          << "Avaliacoes do decoder: " << decoder.evaluation_count() << '\n'
		          << "Tempo: " << std::fixed << std::setprecision(3) << elapsed.count() << " s\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "Erro ao executar o BRKGA: " << error.what() << '\n';
		return 1;
	}

	return 0;
}
