#pragma once

/*
	A little goblin which gleefully adjusts settings to maintain FPS.
		It is based on the knapsack algorithm in knapsack.h.
	
	The goblin controls a list of settings, each of which has some options.
		Each option is assigned a subjective numerical "experience value".
		Settings may provide measurements of their performance cost.

	The goblin selects one option for each setting each frame.
		It tries to maximize experience value without exceeding a cost limit.
		It tracks recent and overall costs to estimate cost accurately.
*/

#include <unordered_map> // Goblin's estimate map
#include <unordered_set> // Goblin's estimate map
#include <string>        // Goblin's estimate map

#include "knapsack.h"


namespace perf_goblin
{
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
		using Decision_t     = typename Knapsack_t::Decision;
		using Option_t       = typename Knapsack_t::Option;

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

			// We store blind guess in past run (because these are mutually exclusive)
			burden_t &blind_guess() const    {return past_run._mk;}
		};

		struct Estimates
		{
			size_t         data_count = 0;
			choice_index_t count;
			bool           fully_explored = false;
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
		using Decision_t     = typename Knapsack_t::Decision;
		using Option_t       = typename Knapsack_t::Option;

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
			auto &decision = setting->decision();

			auto &estimates = estimates_for(setting->id(), decision.option_count);

			if (measure.valid())
			{
				assert(measure.choice < decision.option_count);

				auto &estimate  = estimates.estimates[measure.choice];
				if (!estimate.this_run) ++estimates.count;

				++estimates.data_count;
				estimate.this_run.push      (measure.burden);
				estimate.recent  .push_decay(measure.burden, config.recent_alpha);

				if (estimate.past_run)
				{
					proportion.past    += estimate.past_run.mean();
					proportion.present += measure.burden;
				}
			}
		}

		// Calculate proportion
		auto ratio = proportion.ratio();

		// Calculate estimated burden for all options
		for (auto *setting : settings)
		{
			auto &decision = setting->decision();

			auto &estimates = estimates_for(setting->id(), decision.option_count);

			// Estimate burdens for each choice.
			if (estimates.data_count == 0)
			{
				// Lacking any profiler data from this run, we force to the default choice.
				if (decision.choice >= decision.option_count) decision.choice = 0;
				for (choice_index_t i = 0; i < decision.option_count; ++i)
				{
					decision.options[i].burden =
						((i == decision.choice) ? economy_t::trivial() : economy_t::impossible());
				}
			}
			else
			{
				// Estimate burdens for each option, if possible.
				estimates.fully_explored = true;
				choice_index_t untried_options = 0;
				burden_t lightest = economy_t::impossible();
				for (choice_index_t i = 0; i < decision.option_count; ++i)
				{
					auto &opt = decision.options[i];
					auto &est = estimates.estimates[i];

					if (est.this_run)
					{
						if (est.this_run.count() < config.measure_quota)
						{
							// Blend recent data from this run.
							float mix = est.this_run.count() / config.measure_quota;
							opt.burden =
								(    mix) * est.this_run.mean_plus_sigmas(config.pessimism_sd) +
								(1.f-mix) * ratio * est.past_run.mean_plus_sigmas(config.pessimism_sd);
						}
						else
						{
							// Estimate based on recent measurements
							opt.burden = est.recent.mean_plus_sigmas(config.pessimism_sd);
						}

						if (economy_t::lesser(opt.burden, lightest)) lightest = opt.burden;
					}
					else if (est.past_run)
					{
						// Estimate by comparing measurements from a past session.
						opt.burden = ratio * est.past_run.mean_plus_sigmas(config.pessimism_sd);

						if (economy_t::lesser(opt.burden, lightest)) lightest = opt.burden;
					}
					else
					{
						// This option is unexplored and inestimable.  We'll guess it below.
						estimates.fully_explored = false;
						++untried_options;
					}
				}

				// Blindly guess at inestimable burdens
				if (!estimates.fully_explored)
				{
					// Calculate a uniform blind guess for all burdens.
					burden_t guess = lightest *
						((config.measure_quota * untried_options) / estimates.data_count);

					for (choice_index_t i = 0; i < decision.option_count; ++i)
					{
						auto &estimate  = estimates.estimates[i];
						if (estimate.past_run) continue;
						estimate.blind_guess() = guess;
						if (estimate.this_run) continue;
						decision.options[i].burden = guess;
					}
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
