#pragma once

/*
	C++ implementation of the multiple-choice knapsack algorithm.
		Attempts to find the combination of options which optimizes value,
		while keeping net burden below a hard limit.

	In typical use-cases, burden is proportional to CPU or GPU time.
		Maximum burden is chosen to maintain a desirable framerate or
		meet a deadline of some other kind (eg, for audio rendering).

	The algorithm is most useful when burden values come from live profiling.
		It's also possible to use estimated proportional burden values,
		dynamically adjusting the maximum burden based on overall performance.
*/

#include <limits>    // std::numeric_limits
#include <algorithm> // std::min, std::max, std::lower_bound
#include <cassert>
#include <vector>
#include <unordered_map> // Goblin's estimate map
#include <unordered_set> // Goblin's estimate map
#include <string>        // Goblin's estimate map


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

	
	template<typename T_Economy> class Setting_;
	using Setting = Setting_<Economy_f>;

	/*
		The goblin!
			It takes control of a number of settings and their profile data.
	*/
	template<typename T_Economy>
	class Goblin_
	{
	public:
		using economy_t      = T_Economy;
		using burden_t       = typename economy_t::burden_t;
		using value_t        = typename economy_t::value_t;

		using Setting_t      = Setting_<economy_t>;

		using Knapsack_t     = Knapsack_<economy_t>;
		using choice_index_t = typename Knapsack_t::choice_index_t;
		using Decision_t     = typename Knapsack_t::decision_t;
		using Option_t       = typename Knapsack_t::option_t;

		using strategy_index_t = choice_index_t;

		/*
			A measurement...
				Measured burden should ALWAYS be >= 0.
		*/
		struct Measurement
		{
			burden_t         burden   = economy_t::impossible();
			choice_index_t   choice   = Knapsack_t::CHOICE_NONE;
			//strategy_index_t strategy = Knapsack_t::CHOICE_NONE;

			bool valid() const    {return choice != Knapsack_t::CHOICE_NONE;}
		};

		struct Estimate
		{
			using burden_stat_t = typename economy_t::burden_stat_t;

			burden_stat_t past_run;
			burden_stat_t this_run;
			burden_stat_t recent;
		};

		struct Estimates
		{
			size_t         data_count = 0;
			choice_index_t count;
			Estimate       estimates[];

			Estimates(choice_index_t option_count)    : count(option_count) {}

			static Estimates *alloc(choice_index_t count)
			{
				return new (new size_t[(sizeof(Estimates) + count * sizeof(Estimate) + sizeof(size_t)-1) / sizeof(size_t)]) Estimates(count);
			}
			static void free(Estimates *e)    {delete[] reinterpret_cast<size_t*>(e);}
		};

		struct Config
		{
			float recent_alpha  = .99f;
			float measure_quota = 100;
			float pessimism_sd  = 2.0f;
		};

	public:
		Config config;

	private:
		Knapsack_t                                  knapsack;
		std::unordered_map<std::string, Estimates*> estimates;
		std::unordered_set<Setting_t*>              settings;

		Estimates &estimates_for(const std::string &id, choice_index_t option_count)
		{
			auto i = estimates.find(id);
			if (i == estimates.end()) i = estimates.insert(Estimates::value_type(id, Estimates::alloc(option_count)));
			assert(i->second.count == option_count);
			return *i->second;
		}

		struct Proportionality
		{
			burden_t past = 0, present = 0;

			burden_t ratio() const
			{
				if (past > 0 && present > 0) return present / past;
				return 0;
			}
		}
			proportion;

	public:
		Goblin_();
		~Goblin_();

		/*
			Add & remove settings.
		*/
		void add   (Setting_t *setting);
		void remove(Setting_t *setting);

		/*
			
		*/
		void update();

		
	};

	using Goblin = Goblin_<Economy_f>;
	
	/*
		A setting which can be controlled by the goblin.
	*/
	template<typename T_Economy>
	class Setting_
	{
	public:
		using economy_t      = T_Economy;
		using burden_t       = typename economy_t::burden_t;
		using value_t        = typename economy_t::value_t;

		using Goblin_t         = Goblin_<economy_t>;
		using Measurement      = typename Goblin_t::Measurement;
		using strategy_index_t = typename Goblin_t::strategy_index_t;

		using Knapsack_t     = Knapsack_<economy_t>;
		using choice_index_t = typename Knapsack_t::choice_index_t;
		using Decision_t     = typename Knapsack_t::decision_t;
		using Option_t       = typename Knapsack_t::option_t;

	private:
		friend class Goblin_<economy_t>;
		Goblin_t *_goblin = nullptr;

	protected:
		Decision_t _decision;

	public:
		Setting_(const Option_t *options, choice_index_t option_count)
		{
			_decision.options = options;
			_decision.option_count = option_count;
		}

		template<size_t N>
		Setting_(const Option_t (&options)[N])
		{
			_decision.options = options;
			_decision.option_count = N;
		}

		~Setting_()
		{
			if (_goblin) _goblin->remove(this);
		}

		/*
			Access the decision.
		*/
		const Decision_t &decision()    {return _decision;}

		/*
			Set a strategy number.
		*/
		void set_strategy(strategy_index_t i) const    {}

		/*
			Get burden measurement.  (burden_info_t::impossible() if N/A)
		*/
		virtual Measurement measurement() = 0;
	};


	/*
		Goblin implementation...
	*/
	template<typename Econ>
	Goblin_<Econ>::Goblin_()
	{
	}

	template<typename Econ>
	Goblin_<Econ>::~Goblin_()
	{
		while (settings.size()) remove(*settings.begin());
		for (auto &i : estimates) Estimates::free(i.second);
	}

	template<typename Econ>
	void Goblin_<Econ>::add   (Setting_t *setting)
	{
		if (setting->_goblin) return setting->_goblin == this;
		setting->_goblin = this;
		if (settings.find(setting) != settings.end()) return true;
		settings.insert(setting);
		return true;
	}
	template<typename Econ>
	void Goblin_<Econ>::remove(Setting_t *setting)
	{
		settings.erase(setting);
		if (setting->_goblin == this) setting->_goblin = nullptr;
	}

	template<typename Econ>
	void Goblin_<Econ>::update()
	{
		knapsack.clear();

		// TODO deal with lack of past-run profiling info

		// Harvest any new measurements
		for (auto *setting : settings)
		{
			auto measure = setting->measurement();

			if (measure.valid())
			{
				assert(measure.choice < setting->decision.option_count);

				auto &estimates = estimate_for(setting->id(), setting->decision.option_count);
				auto &estimate  = estimates.estimates[measure.choice];

				++estimates.data_count;
				estimate.this_run.push      (measure.burden);
				estimate.recent  .push_decay(measure.burden, config.recent_alpha);

				if (estimate.past_run._k)
				{
					proportion.past    += estimate.past_run.mean();
					proportion.present += measure.burden;
				}
			}
		}

		// Calculate proportion
		auto ratio = proportion.ratio();

		// Calculate estimated burden for all options
		for (auto &pair : settings)
		{
			auto *setting = pair.first;
			auto &estimates = pair.second;

			auto &decision = setting->decision();

			// Estimate burdens for each choice.
			if (estimates.data_count)
			{
				// Calculate estimated burden for each choice...
				for (choice_index_t i = 0; i < decision.option_count; ++i)
				{
					auto &opt = decision.options[i];
					auto &est = estimates.estimates[i];

					if (est.this_run._k > 0)
					{
						// Estimate based on measurements
						opt.burden = est.recent.mean_plus_sigmas(config.pessimism_sd);

						// If below quota, mix with proportional past estimate
						if (est.this_run._k < config.measure_quota)
						{
							float mix = est.this_run._k / config.measure_quota;
							opt.burden =
								(    mix) * opt.burden +
								(1.f-mix) * ratio * est.past_run.mean_plus_sigmas(config.pessimism_sd);
						}
					}
					else
					{
						// No data; use proportional past estimate.
						opt.burden = ratio * est.past_run.mean_plus_sigmas(config.pessimism_sd);
					}
				}
			}
			else
			{
				// Lacking any profiler data from this run, we force to the default choice.
				if (decision.choice >= decision.option_count) decision.choice = 0;
				for (choice_index_t i = 0; i < decision.option_count; ++i)
				{
					decision.options[i].burden =
						((i == decision.choice) ? economy_t::trivial() : economy_t::impossible());
				}
			}

			// Add this decision to the knapsack.
			knapsack.add_decision(&decision);
		}

		// Finally, run the knapsack solver.
		knapsack.decide();
	}
}


#if 0
/*
			Add an unavoidable burden.
		*/
		Decision& add_burden(burden_t burden)
		{
			decisions.emplace_back(Decision());
			Decision &decision = decisions.back();
			decision.options.push_back(Option{burden,0});
			return decision;
		}

		/*
			Add an fixed incentive (modifies the total value of any solution)
		*/
		Decision& add_incentive(value_t value)
		{
			decisions.emplace_back(Decision());
			Decision &decision = decisions.back();
			decision.options.push_back(Option{0,value});
			return decision;
		}

		/*
			Add a binary decision which has no burden 
		*/
		Decision& add_binary_item(burden_t burden, value_t value)
		{
			decisions.emplace_back(Decision());
			Decision &decision = decisions.back();
			decision.options.push_back(Option{0,0});
			decision.options.push_back(Option{burden,value});
			return decision;
		}

		/*
			Add an option from an array.
		*/
		template<size_t N>
		Decision& add_decision(const Option (&options)[N])
		{
			decisions.emplace_back(Decision());
			Decision &decision = decisions.back();
			for (const Option &option : options) decision.options.push_back(option);
			return decision;
		}
#endif
