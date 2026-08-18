#ifndef TDRP_DECODER_H
#define TDRP_DECODER_H

#include "../common/graph.hpp"
#include <atomic>
#include <memory>
#include <vector>

class TDRPDecoder
{
public:
	explicit TDRPDecoder(const graph *graph);

	double decode(const std::vector<double> &chromosome) const;
	uint64_t evaluation_count() const;

private:
	const graph *graph_;
	std::shared_ptr<std::atomic<uint64_t>> evaluation_counter;
};

#endif
