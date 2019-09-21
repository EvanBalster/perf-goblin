#pragma once


/*
	C++ implementation of the multiple-choice knapsack algorithm.
		Attempts to find the combination of options which optimizes value,
		while keeping net burden below a defined capacity.

	In typical use-cases, burden is proportional to CPU or GPU time.
		Maximum burden is chosen to maintain a desirable framerate or
		meet a deadline of some other kind (eg, for audio rendering).

	The algorithm is most useful when burden values come from live profiling.
		It's also possible to use estimated proportional burden values,
		dynamically adjusting the maximum burden based on overall performance.
		The Goblin class implements the former approach.
*/


#include <limits>    // std::numeric_limits
#include <algorithm> // std::min, std::max, std::lower_bound
#include <cassert>
#include <vector>
#include <cstdint>

#include "economy.h"


namespace perf_goblin
{
	/*
		This class models the multiple-choice knapsack problem.
			
	*/
	template<typename T_Economy>
	struct Knapsack_
	{
	public:
		static const bool EnableProfiling = true;

		using economy_t      = T_Economy;
		using burden_t       = typename economy_t::burden_t;
		using capacity_t     = typename economy_t::capacity_t;
		using value_t        = typename economy_t::value_t;

		using index_t        = size_t;
		using choice_index_t = uint16_t;
		using score_t        = ptrdiff_t;

		static const choice_index_t NO_CHOICE = ~choice_index_t(0);


		// A single item that may be chosen.
		struct Option
		{
			// Burden represents a cost applying to some finite resource.
			//   The algorithm keeps total burden under some maximum.
			//   This value is typically positive (but not required to be).
			burden_t burden;

			// Value represents the benefit or detriment from picking this option.
			//   The algorithm tries to maximize value.
			//   This value may be positive or negative.
			value_t  value;

			// -- remaining members are automatically-computed --

			// Quantized value used in the algorithm.
			mutable score_t score;


			// Is this option possible?
			bool possible() const    {return economy_t::is_possible(burden);}
		};

		// A choice among some number of items.
		struct Decision
		{
			const Option  *options      = nullptr;
			choice_index_t option_count = 0;

			// Iterable.
			const Option *begin() const    {return options;}
			const Option *end  () const    {return options+option_count;}

			// The current choice among options.
			//    This is overwritten by the algorithm.
			choice_index_t
				choice      = 0, // Algorithm's choice.
				choice_easy = 0, // Lowest-burden choice.
				choice_high = 0; // Highest-value choice.

			// Access options after running the solver
			const Option &chosen()      const    {return options[choice];}
			const Option &option_easy() const    {return options[choice_easy];}
			const Option &option_high() const    {return options[choice_high];}

			// Refresh choice_easy and choice_high.
			void refresh_range()
			{
				choice_easy = choice_high = 0;
				if (option_count == 0) return;

				const Option &first = options[0];
				burden_t easy_burden = first.burden;
				value_t high_value = (first.possible() ? first.value : -std::numeric_limits<value_t>::infinity());

				for (choice_index_t i = 1; i < option_count; ++i)
				{
					const Option &option = options[i];
					if (option.value > high_value && option.possible()) {high_value  = option.value;  choice_high = i;}
					if (economy_t::lesser(option.burden, easy_burden))  {easy_burden = option.burden; choice_easy = i;}
				}
			}
		};

		// Statistics describing a set of decisions.
		struct Stats
		{
			burden_t net_burden = economy_t::zero();
			value_t  net_value  = economy_t::zero();
			score_t  net_score  = 0;

			Stats &operator+=(const Option &o)
			{
				net_burden += o.burden;
				net_value  += o.value;
				net_score += o.score;
				return *this;
			}
		};

		// Internal: used to track lightest solution
		struct Minimum
		{
			score_t        net_score  = score_t(0);
			burden_t       net_burden = economy_t::infinite();
			choice_index_t choice     = NO_CHOICE;

			// Ordering
			bool operator<(const Minimum &o) const    {return net_score < o.net_score;}

			// Validity
			bool valid() const                        {return choice != NO_CHOICE;}

			// Update with an alternative, if it is lighter
			void consider(const Minimum &other)       {if (economy_t::lesser(other.net_burden, net_burden)) *this = other;}
		};

		// Internal: used to track lightest solution for every combination of subset/score
		struct Minimums
		{
			std::vector<Minimum> store;
			std::vector<size_t>  row_end;

			void clear()
			{
				store.clear();
				row_end.clear();
			}

			// Query a minimum
			Minimum operator()(index_t row, score_t score) const
			{
				// Determine the row we're searching
				auto
					rbeg = store.begin() + (row ? row_end[row-1] : 0u),
					rend = store.begin() + row_end[row];
				Minimum search;
				search.net_score = score;

				// Find an element with the given score.
				auto pos = std::lower_bound(rbeg, rend, search);
				if (pos != rend) search = *pos;
				return search;
			}

			// Select the high-value strategy for a given burden limit.
			Minimum decide(const capacity_t capacity, index_t row)
			{
				index_t i = row_end[row], e = (i ? row_end[row-1] : 0u);
				while (i-- > e)
					if (economy_t::acceptable(store[i].net_burden, capacity)) return store[i];
				return Minimum();
			}
			Minimum decide(const capacity_t capacity)
			{
				return decide(capacity, row_end.size()-1);
			}
		};

	public:
		// Set of decisions to fill in
		std::vector<Decision*> decisions;

		// Metadata generated by the algorithm.
		Minimums   minimums;
		
		struct ProblemStats
		{
			Stats   chosen, highest, lightest;
			size_t  iterations           = 0;
			value_t value_to_score_scale = 0;
		}
			stats;

	public:
		void clear()
		{
			decisions.clear();
			minimums.clear();
			stats = ProblemStats();
		}
		void add_decision(Decision *decision)
		{
			decisions.push_back(decision);
		}

		/*
			Evaluate all decisions, selecting exactly one option for each and overwriting
				the 'option' field to that option's index.

			capacity -- sets a limit on the total burden of selected options.
				If all solutions exceed this limit, the lowest-burden solution is chosen.

			precision -- governs the algorithm's optimality and efficiency.
				The solution's net value will be at least (100 - 100/precision)% of optimal.
				Runtime increases linearly with precision.
		*/
		bool decide(capacity_t capacity, size_t precision = 50)
		{
			precision = std::max<size_t>(precision, 4);

			stats.iterations = 0;

			// Prepare algorithm (performs value->score scaling)
			_prepare(precision);

			// Shortcut: if the lightest solution is overburdened, return it (failure)
			if (!economy_t::acceptable(stats.lightest.net_burden, capacity))
			{
				for (Decision *decision : decisions) decision->choice = decision->choice_easy;
				stats.chosen = stats.lightest;
				return false;
			}

			// Shortcut: if the highest-valued solution is not overburdened, return it
			if (economy_t::acceptable(stats.highest.net_burden, capacity))
			{
				// Load the choice as noted in _prepare, and return it.
				for (Decision *decision : decisions) decision->choice = decision->choice_high;
				stats.chosen = stats.highest;
				return true;
			}

			// Sort all decisions by maximum value
			std::sort(decisions.begin(), decisions.end(),
				[](const Decision *l, const Decision *r) {return l->option_high().score < r->option_high().score;});

			// Compute the table of minimums...
			_compute_minimums(capacity);

			// Identify the highest-scoring solution that is not over-burden
			{
				Minimum solution = minimums.decide(capacity);

				index_t i = decisions.size();
				while (true)
				{
					--i;
					Decision &decision = *decisions[i];
					assert(solution.choice <= decision.option_count);
					decision.choice = solution.choice;
					score_t next_score = solution.net_score - decision.chosen().score;
					if (i == 0)
					{
						assert(next_score == 0);
						break;
					}
					solution = minimums(i-1, next_score);
				}
			}

			// Calculate final stats and return.
			stats.chosen = Stats();
			for (Decision *decision : decisions) stats.chosen += decision->chosen();
			assert(economy_t::acceptable(stats.chosen.net_burden, capacity));
			
			return true;
		}

	private:

		/*
			Main algorithm:
				* Find lightest solution per score for every subset 0..i
		*/
		void _compute_minimums(const capacity_t &capacity)
		{
			minimums.clear();

			std::vector<Minimum>
				previous,
				current;

			previous.reserve(stats.highest.net_score);
			current .reserve(stats.highest.net_score);

			auto consider = [&](const Minimum &candidate)
			{
				// optimization?: don't even consider over-capacity candidates.
				if (!economy_t::acceptable(candidate.net_burden, capacity)) return;
				assert(economy_t::is_possible(candidate.net_burden));
				
				// Add to nonsparse list
				if (candidate.net_score >= score_t(current.size()))
					current.resize(candidate.net_score + 1);
				current[candidate.net_score].consider(candidate);

				// Consolidate into current row (which is sorted)
				/*auto existing = std::lower_bound(current.begin(), current.end(), candidate);
				if (existing == current.end()) current.push_back(candidate);
				else if (existing->net_score > candidate.net_score) current.insert(existing, candidate);
				else existing->consider(candidate.choice, candidate.net_burden);*/
			};

			for (index_t i = 0; i < decisions.size(); ++i)
			{
				Decision &decision = *decisions[i];

				for (choice_index_t choice_index = 0, e = decision.option_count; choice_index < e; ++choice_index)
				{
					// Only consider options with non-negative value.
					const Option &option = decision.options[choice_index];
					if (option.score < 0 || !option.possible()) continue;

					if (i == 0)
					{
						Minimum candidate;
						candidate.net_burden = option.burden;
						candidate.net_score  = option.score;
						candidate.choice     = choice_index;
						
						consider(candidate);

						if (EnableProfiling) ++stats.iterations;
					}
					else
						for (const Minimum &base : previous) // TODO fewer iterations?
					{
						// Find minimum 
						Minimum candidate = base;
						candidate.net_burden += option.burden;
						candidate.net_score  += option.score;
						candidate.choice     = choice_index;

						consider(candidate);

						if (EnableProfiling) ++stats.iterations; // maybe inc by log2(current.size())
					}
				}

				previous.clear();

				// Copy the row into our sparse table.
				for (auto &min : current)
					if (min.valid())
				{
					previous.push_back(min);
					minimums.store.push_back(min);
				}
				minimums.row_end.push_back(minimums.store.size());

				// Save the row for use in the next row.
				//std::swap(previous, current);
				current.clear();
			}
		}

		// Prepare algorithm
		void _prepare(size_t precision)
		{
			value_t max_value_range = 0;

			stats.lightest = Stats();
			stats.highest = Stats();

			/*
				First pass:
					* ascertain lightest item(s) and pick them by default
					* ascertain maximum range of relevant values
			*/
			for (Decision *decision : decisions) if (decision->option_count)
			{
				// Detect low-burden and high-value options.
				decision->refresh_range();

				// Look for the range of values
				const Option
					&easy = decision->option_easy(),
					&high = decision->option_high();

				easy.score = 0;
				stats.lightest += easy;

				max_value_range = std::max(max_value_range, high.value - easy.value);
			}
			if (max_value_range <= 0) max_value_range = 1;

			/*
				Second pass:
					* score each option, up to max score <precision>.
					* calculate the highest-valued solution (and max net score).
			*/
			const value_t value_to_score_scale = precision / max_value_range;
			stats.value_to_score_scale = value_to_score_scale;

			for (Decision *decision : decisions)
			{
				// Searching for the highest-value option.
				value_t value_min = decision->option_easy().value;

				for (const Option &option : *decision)
				{
					// Calculate quantized value ("score").
					option.score = score_t(std::ceil((option.value - value_min) * value_to_score_scale));
				}

				// Add lightest option to lightest total.
				stats.highest += decision->option_high();
			}
		}
	};

	using Knapsack = Knapsack_<Economy_f>;
}
