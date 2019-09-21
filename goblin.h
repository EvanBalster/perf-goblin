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

#include <unordered_map> // Goblin's settings map

#include "knapsack.h"
#include "economy.h"
#include "profile.h"


namespace perf_goblin
{
	
	template<typename T_Economy>     class Goblin_;
	template<typename T_Economy>     class Setting_;

	using Goblin  = Goblin_ <Economy_f>;
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
		using scalar_t       = typename economy_t::scalar_t;
		using value_t        = typename economy_t::value_t;

		using Profile_t      = Profile_<economy_t>;
		using economy_norm_t = typename Profile_t::economy_norm_t;;
		using burden_norm_t  = typename economy_norm_t::burden_t;
		using burden_stat_t  = typename Profile_t::burden_stat_t;
		using capacity_t     = typename economy_norm_t::capacity_t;

		using Knapsack_t     = Knapsack_<economy_norm_t>;
		using choice_index_t = typename Knapsack_t::choice_index_t;
		using Decision_t     = typename Knapsack_t::Decision;
		using Option_t       = typename Knapsack_t::Option;

		using Setting_t        = Setting_<economy_t>;
		using strategy_index_t = choice_index_t;

		static const choice_index_t NO_CHOICE = Knapsack_t::NO_CHOICE;

		
		using Settings  = std::unordered_map<Setting_t*, Decision_t>;

		struct Config
		{
			scalar_t recent_alpha  = 1.f - 1.f/30.f;
			scalar_t anomaly_alpha = 1.f - 1.f/30.f;
			scalar_t measure_quota = 30;
			value_t  explore_value = 0;
		};

		struct Anomaly
		{
			scalar_t latest = 1;
			scalar_t recent = 1;
		};

	public:
		Config config;

	private:
		Profile_t             _profile, _past;
		Settings              settings;
		Knapsack_t            _knapsack;
		std::vector<Option_t> option_store;
		Anomaly               _anomaly;

	public:
		Goblin_();
		~Goblin_();

		/*
			Overwrite performance profiles.
		*/
		void set_profile     (const Profile_t &profile)    {_profile = profile;}
		void set_past_profile(const Profile_t &profile)    {_past    = profile;}

		/*
			Add & remove settings.
		*/
		bool add   (Setting_t *setting);
		void remove(Setting_t *setting);

		/*
			Update all settings, accounting for any new measurements.
				Alternatively, you can call subroutines separately:
				- harvest() : collect new measurements and update anomaly
				- decide(...) : resolve all settings
		*/
		void update(capacity_t capacity, size_t precision);
		void decide(capacity_t capacity, size_t precision);
		void harvest();

		/*
			Access the profile(s) and knapsack solver (for stats)
		*/
		const Knapsack_t &knapsack()     const    {return _knapsack;}
		const Anomaly    &anomaly()      const    {return _anomaly;}
		const Profile_t  &profile()      const    {return _profile;}
		const Profile_t  &past_profile() const    {return _past;}

		/*
			Get a consolidated profile of past and current-run knowledge.
		*/
		Profile_t         full_profile() const
		{
			scalar_t ratio = past_present_ratio();
			if (ratio <  0) return _profile;
			if (ratio == 0) return _past;
			Profile_t profile = _profile;
			for (auto &t : _past.tasks())
				profile.assimilate(t.first, *t.second, ratio);
			return profile;
		}

		/*
			Calculate burden ratio between past and present profile.
		*/
		scalar_t past_present_ratio() const
		{
			scalar_t total_ratio = 0;
			scalar_t total_weight = 0;
			for (auto &t : _profile.tasks())
			{
				auto *curr = t.second;
				if (auto *prev = _past.find(t.first))
				{
					for (choice_index_t i = 0; i < curr->count; ++i)
					{
						auto &cest = curr->estimates[i].full, &pest = prev->estimates[i].full;
						scalar_t w = cest.count() * pest.count();
						if (w > 0)
						{
							w = std::sqrt(w * cest.mean() * pest.mean());
							total_ratio += w * (cest.mean() / pest.mean());
							total_weight += w;
						}
					}
				}
			}
			if (total_weight > 0) return total_ratio / total_weight;
			else                  return scalar_t(-1);
		}

		/*
			Access decision for a setting
		*/
		const Decision_t *get_decision(Setting_t *setting) const
		{
			auto i = settings.find(setting);
			return (i==settings.end()) ? nullptr : &i->second;
		}
	};
	
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

		using Profile_t      = Profile_<economy_t>;
		using Measurement    = typename Profile_t::Measurement;

		using Goblin_t         = Goblin_<economy_t>;
		
		using strategy_index_t = typename Goblin_t::strategy_index_t;
		using choice_index_t   = typename Goblin_t::choice_index_t;

		static const choice_index_t NO_CHOICE = Goblin_t::NO_CHOICE;

		struct Option
		{
			value_t value;
		};
		struct Options
		{
			const Option  *options;
			choice_index_t option_count;

			// Iterable.
			const Option *begin() const    {return options;}
			const Option *end  () const    {return options+option_count;}
		};

	private:
		friend class Goblin_<economy_t>;
		Goblin_t *_goblin = nullptr;

	public:
		Setting_() {}

		~Setting_()
		{
			if (_goblin) _goblin->remove(this);
		}

		/*
			Get the set of options
		*/
		virtual const Options &options() = 0;

		/*
			Choice management
				choice_default: suggested choice; may change at any time.
				choice_set: updated choice from goblin.
		*/
		virtual choice_index_t choice_default() const    {return 0;}
		virtual void           choice_set(
			choice_index_t   choice_index,
			strategy_index_t strategy_index) = 0;

		/*
			Get ID for estimation purposes
		*/
		virtual const std::string &id() const = 0;

		/*
			Get burden measurement.  (burden_info_t::infinite() if N/A)
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
		while (settings.size()) remove(settings.begin()->first);
	}

	template<typename Econ>
	bool Goblin_<Econ>::add   (Setting_t *setting)
	{
		if (setting->_goblin) return setting->_goblin == this;
		setting->_goblin = this;
		if (settings.find(setting) != settings.end()) return true;
		settings.emplace(setting, Decision_t());
		return true;
	}
	template<typename Econ>
	void Goblin_<Econ>::remove(Setting_t *setting)
	{
		settings.erase(setting);
		if (setting->_goblin == this) setting->_goblin = nullptr;
	}

	template<typename Econ>
	void Goblin_<Econ>::harvest()
	{
		// Decay old measurements
		_profile.decay_recent(config.recent_alpha);

		// Sums for calculating anomaly
		burden_t
			sum_typical = economy_t::zero(),
			sum_current = economy_t::zero();

		// Harvest any new measurements
		for (auto &pair : settings)
		{
			auto *setting = pair.first;
			auto measure = setting->measurement();

			// Measure...
			if (measure.valid())
			{
				// Burdens must be >= 0.
				if (economy_t::lesser(measure.burden, economy_t::zero()))
					measure.burden = economy_t::zero();

				// Compare with existing metrics to calculate anomaly.
				auto entry = _profile.find(setting->id());
				if (entry)
				{
					sum_typical += entry->estimates[measure.choice].full.mean();
					sum_current += measure.burden;
				}

				// Collect into profile.
				_profile.collect(
					setting->id(),
					setting->options().option_count,
					measure);
			}
		}

		// Estimate anomaly
		if (economy_t::lesser(economy_t::zero(), sum_typical))
		{
			_anomaly.latest = sum_current / sum_typical;
			_anomaly.recent += (1 - config.anomaly_alpha) * (_anomaly.latest - _anomaly.recent);
		}
	}

	template<typename Econ>
	void Goblin_<Econ>::decide(capacity_t capacity, size_t precision)
	{
		_knapsack.clear();
		option_store.clear();

		// Calculate proportion between past-run costs and this-run costs.
		scalar_t ratio = past_present_ratio();

		static const burden_stat_t UNKNOWN_BURDEN = {};

		// Calculate estimated burden for all options and generate a knapsack problem
		for (auto &pair : settings)
		{
			auto *setting = pair.first;
			auto &decision = pair.second;
			const typename Setting::Options &options = setting->options();
			decision.option_count = options.option_count;

			// Get profile data for this task
			auto *pres = _profile.find(setting->id());
			auto *past = _past   .find(setting->id());

			// Estimate burdens for each choice.
			if (pres || (past && ratio > scalar_t(0)))
			{
				// Calculate a blind guess for unprofiled options?
				burden_norm_t blind_guess;
				scalar_t      unexplored_burden_mod = scalar_t(1);
				scalar_t      missing_data = 0;
				if (!pres || !pres->fully_explored)
				{
					burden_norm_t lightest = economy_norm_t::infinite();

					for (choice_index_t i = 0; i < decision.option_count; ++i)
					{
						const burden_stat_t &
							curr = (pres ? pres->estimates[i].full : UNKNOWN_BURDEN),
							prev = (past ? past->estimates[i].full : UNKNOWN_BURDEN);

						burden_norm_t test;
						float count = 0;
						if      (curr) {count = curr.count(); test = curr.burden_norm() * _anomaly.recent;}
						else if (prev) {count = prev.count(); test = prev.burden_norm() * ratio;}

						missing_data += std::max<scalar_t>(0,
							config.measure_quota - curr.count() - prev.count());
						if (count && economy_norm_t::lesser(test, lightest)) lightest = test;
					}

					blind_guess = lightest;
					unexplored_burden_mod = missing_data / std::max<scalar_t>(missing_data,
						(pres ? pres->data_count : 0) +
						(past ? past->data_count : 0));
					if (pres) pres->fully_explored = (!missing_data);
				}

				// Estimate burdens for each option, if possible.
				for (choice_index_t i = 0; i < decision.option_count; ++i)
				{
					burden_norm_t option_burden;
					value_t value_bonus = economy_t::zero();

					const burden_stat_t &
						recent = (pres ? pres->estimates[i].recent : UNKNOWN_BURDEN),
						curr   = (pres ? pres->estimates[i].full   : UNKNOWN_BURDEN),
						prev   = (past ? past->estimates[i].full   : UNKNOWN_BURDEN);

					// Prior burden is based on past runs, or failing that a blind guess.
					burden_norm_t prior_burden = (prev ?
						(prev.burden_norm() * ratio) :
						blind_guess);

					if (curr)
					{
						if (curr.count() < config.measure_quota)
						{
							// Interpolate between data from this run and prior estimate.
							float mix = curr.count() / config.measure_quota;
							option_burden =
								curr.burden_norm() * mix +
								prior_burden       * (1.f-mix);
						}
						else
						{
							// TODO mix with full when recent measures are few

							// Estimate based on recent measurements
							option_burden = recent.burden_norm();
						}
					}
					else
					{
						// No profiling data from this run.  Use past data or blind guess.
						option_burden = prior_burden;
					}

					// Incentive to explore options further...
					if (prev.count() + curr.count() < config.measure_quota)
					{
						value_bonus    = config.explore_value;
						option_burden *= unexplored_burden_mod;
						if (pres) pres->fully_explored = false;
					}

					// Formulate option for knapsack decision...
					option_store.push_back(Option_t{
						option_burden,
						options.options[i].value + value_bonus});
				}
			}
			else
			{
				// Lacking any profiler data from this run, we force to the default choice.
				if (decision.choice >= decision.option_count) decision.choice = 0;
				for (choice_index_t i = 0; i < options.option_count; ++i)
				{
					option_store.push_back(Option_t{
						{(i == decision.choice) ? burden_t(economy_t::zero()) : burden_t(economy_t::infinite())},
						options.options[i].value});
				}
			}

			// Add this decision to the knapsack.
			_knapsack.add_decision(&decision);
		}

		// Associate all decisions with options
		{
			size_t i = 0;
			for (auto &pair : settings)
			{
				pair.second.options = &option_store[i];
				i += pair.second.option_count;
			}
		}

		/*if (ratio > 0)
		{
			std::cout << std::flush;
		}*/

		// Finally, run the knapsack solver.
		_knapsack.decide(capacity, precision);

		// Then apply all choices
		for (auto &pair : settings)
		{
			pair.first->choice_set(pair.second.choice, 0);
		}
	}

	template<typename Econ>
	void Goblin_<Econ>::update(capacity_t capacity, size_t precision)
	{
		// Harvest new measurements
		harvest();

		// Decide
		decide(capacity, precision);
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
