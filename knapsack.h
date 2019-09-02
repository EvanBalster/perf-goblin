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


namespace perf_goblin
{
	/*
		An economy describes a burden and a value.
			Value represents something we want to maximize.
			Burden represents quantities of one or more limited resources.
			The most common economy uses a float burden and float value.
	*/
	template<
		typename T_Burden,
		typename T_Value       = float>
	struct Economy_
	{
		using burden_t = T_Burden;
		using value_t  = T_Value;

		// True if the burden value is scalar.
		static const bool burden_is_scalar = true;

		// Combine two burdens.
		static constexpr T_Burden trivial   ()    {return std::numeric_limits<T_Burden>::zero();}
		static constexpr T_Burden impossible()    {return std::numeric_limits<T_Burden>::infinity();}

		// Return whether one burden is strictly less than another.
		static int lesser(const T_Burden &lhs, const T_Burden &rhs)    {return (lhs < rhs);}

		// Statistics on burdens.
		struct burden_stat_t
		{
			T_Burden _k  = 0;
			T_Burden _mk = 0;
			T_Burden _vk = 0;

			T_Burden mean     () const    {return _mk;}
			T_Burden variance () const    {return _vk / (_k - 1);}
			T_Burden deviation() const    {return std::sqrt(variance());}

			T_Burden mean_plus_sigmas(float sigmas)    {return mean() + deviation() * sigmas;}

			void push(const T_Burden burden)
			{
				burden_t d = (_k++ ? (burden - _mk) : 0);
				_mk += d / _k;             // First frame, add burden
				_vk += d * (burden - _mk); // First frame, add 0
			}

			// Decay methods for calculating *recent* variance.  0 < alpha < 1.
			void decay     (float alpha)                           {_k = 1 + (_k - 1) * alpha; _vk *= alpha;}
			void push_decay(const T_Burden burden, float alpha)
			{
				_k *= alpha;
				burden_t d = (_k++ ? (burden - _mk) : 0);
				_mk += d / _k;
				_vk  = _vk * alpha + d * (burden - _mk);
			}
		};
	};

	using Economy_f = Economy_<float>;

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

			// Rounded value used in the algorithm.
			mutable score_t score;
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
				choice_min  = 0, // Lowest-burden choice.
				choice_high = 0; // Highest-value choice.

			// Access options after running the solver
			const Option &chosen()      const    {return options[choice];}
			const Option &option_min () const    {return options[choice_min];}
			const Option &option_high() const    {return options[choice_high];}
		};

		// Statistics describing a set of decisions.
		struct Stats
		{
			burden_t net_burden = 0;
			value_t  net_value  = 0;
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
			burden_t       net_burden = economy_t::impossible();
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
			Minimum decide(burden_t capacity, index_t row)
			{
				index_t i = row_end[row], e = (i ? row_end[row-1] : 0u);
				while (i-- > e)
					if (economy_t::lesser(store[i].net_burden, capacity)) return store[i];
				return Minimum();
			}
			Minimum decide(burden_t capacity)
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
		bool decide(burden_t capacity, size_t precision = 50)
		{
			precision = std::max<size_t>(precision, 4);

			stats.iterations = 0;

			// Prepare algorithm (performs value->score scaling)
			_prepare(precision);

			// Shortcut: if the lightest solution is overburdened, return it (failure)
			if (stats.lightest.net_burden > capacity)
			{
				for (Decision *decision : decisions) decision->choice = decision->choice_min;
				stats.chosen = stats.lightest;
				return false;
			}

			// Shortcut: if the highest-valued solution is not overburdened, return it
			if (stats.highest.net_burden <= capacity)
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
				Minimum strategy = minimums.decide(capacity);

				index_t i = decisions.size();
				while (true)
				{
					--i;
					Decision &decision = *decisions[i];
					assert(strategy.choice <= decision.option_count);
					decision.choice = strategy.choice;
					score_t next_score = strategy.net_score - decision.chosen().score;
					if (i == 0)
					{
						assert(next_score == 0);
						break;
					}
					strategy = minimums(i-1, next_score);
				}
			}

			// Calculate final stats and return.
			stats.chosen = Stats();
			for (Decision *decision : decisions) stats.chosen += decision->chosen();
			return true;
		}

	private:

		/*
			Main algorithm:
				* Find lightest solution per score for every subset 0..i
		*/
		void _compute_minimums(const burden_t &capacity)
		{
			minimums.clear();

			std::vector<Minimum>
				previous,
				current;

			current.reserve(stats.highest.net_score);

			auto consider = [&](const Minimum &candidate)
			{
				assert(economy_t::lesser(candidate.net_burden, economy_t::impossible()));
				if (economy_t::lesser(candidate.net_burden, capacity))
				{
					// Add to nonsparse list
					if (candidate.net_score >= score_t(current.size()))
						current.resize(candidate.net_score + 1);
					current[candidate.net_score].consider(candidate);

					// Consolidate into current row (which is sorted)
					/*auto existing = std::lower_bound(current.begin(), current.end(), candidate);
					if (existing == current.end()) current.push_back(candidate);
					else if (existing->net_score > candidate.net_score) current.insert(existing, candidate);
					else existing->consider(candidate.choice, candidate.net_burden);*/
				}
			};

			for (index_t i = 0; i < decisions.size(); ++i)
			{
				Decision &decision = *decisions[i];

				for (choice_index_t choice_index = 0, e = decision.option_count; choice_index < e; ++choice_index)
				{
					// Only consider options with non-negative value.
					const Option &option = decision.options[choice_index];
					if (option.score < 0) continue;

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
				// Look for the range of values
				const Option &first = decision->options[0];
				burden_t light_burden = first.burden, light_value = first.value, max_value = first.value;
				decision->choice_min = 0;
				for (choice_index_t i = 1, e = decision->option_count; i < e; ++i)
				{
					const Option &option = decision->options[i];
					if (option.value  > max_value)
					{
						max_value = option.value;
					}
					if (economy_t::lesser(option.burden, light_burden))
					{
						light_burden = option.burden;
						light_value  = option.value;
						decision->choice_min = i;
					}
				}

				stats.lightest.net_burden += light_burden;
				stats.lightest.net_value  += light_value;

				max_value_range = std::max(max_value_range, max_value - light_value);
			}

			/*
				Second pass:
					* score each option, up to max score <precision>.
					* calculate the highest-valued solution (and max net score).
			*/
			const value_t value_to_score_scale = precision / max_value_range;
			stats.value_to_score_scale = value_to_score_scale;

			for (Decision *decision : decisions)
			{
				value_t value_min = decision->option_min().value;
				const Option &first = decision->options[0];

				// Searching for the highest-value option.
				score_t high_score = -1;
				decision->choice_high = 0;

				for (const Option &option : *decision)
				{
					// Calculate quantized value ("score").
					option.score = score_t(std::ceil((option.value - value_min) * value_to_score_scale));

					// Select most valuable item
					if (option.score > high_score)
					{
						decision->choice_high = choice_index_t(&option - decision->options);
						high_score = option.score;
					}
				}

				// Add lightest option to lightest total.
				stats.highest += decision->option_high();
			}
		}
	};

	using Knapsack = Knapsack_<Economy_f>;
}