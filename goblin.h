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

#include <unordered_map> // Goblin's estimate and setting maps
#include <string>        // Used to classify settings for profiling.

#include "knapsack.h"


namespace perf_goblin
{
	template<typename T_Economy> class Setting_;
	using Setting = Setting_<Economy_f>;

	/*
		An economy for normally-distributed burdens.
			This allows us to consolidate independent variations in cost.
	*/
	template<typename T_BaseEconomy>
	class Economy_Normal_ : public T_BaseEconomy
	{
	public:
		using base_t        = T_BaseEconomy;
		using base_burden_t = typename base_t::burden_t;
		using value_t       = typename base_t::value_t;
		using scalar_t      = typename base_t::scalar_t;

		struct burden_t
		{
			base_burden_t mean;
			base_burden_t var;

			burden_t  operator* (const scalar_t  s) const    {return {mean/s, var/(s*s)};}
			burden_t &operator*=(const scalar_t  s)          {mean *= s; var *= s*s; return *this;}
			burden_t  operator/ (const scalar_t  s) const    {return {mean/s, var/(s*s)};}
			burden_t &operator/=(const scalar_t  s)          {mean /= s; var /= s*s; return *this;}
			burden_t  operator+ (const burden_t &o) const    {return {mean+o.mean, var+o.var};}
			burden_t &operator+=(const burden_t &o)          {mean += o.mean; var += o.var; return *this;}
			burden_t  operator- (const burden_t &o) const    {return {mean-o.mean, var-o.var};}
			burden_t &operator-=(const burden_t &o)          {mean -= o.mean; var -= o.var; return *this;}

			base_burden_t sigma_offset(const scalar_t sigmas) const
			{
				return mean + sigmas * std::sqrt(var);
			}
		};

	public:
		// Sigma offset used when comparing burdens.
		//   This is used to get a high probability of avoiding over-capacity.
		scalar_t sigmas = scalar_t(4);

	public:
		static const bool burden_is_scalar = base_t::burden_is_scalar;

		static constexpr burden_t trivial   ()    {return {base_t::trivial(), base_t::trivial()};}
		static constexpr burden_t impossible()    {return {base_t::impossible(), base_t::trivial()};}
		static constexpr bool is_possible(const burden_t burden)    {return base_t::is_possible(burden.mean) & base_t::is_possible(burden.var);}

		// Return whether one burden is expected to be less than another
		bool lesser(const burden_t &lhs, const burden_t &rhs) const
		{
			return base_t::lesser(lhs.sigma_offset(sigmas), rhs.sigma_offset(sigmas));
			/*base_burden_t diff = (lhs.mean - rhs.mean);
			diff = (diff*diff) / (sigmas*sigmas);
			return base_t::lesser(diff, rhs.var + lhs.var - scalar_t(2)*std::sqrt(lhs.var*rhs.var));*/
		}
	};

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

		using economy_norm_t = Economy_Normal_<economy_t>;
		using burden_norm_t  = typename economy_norm_t::burden_t;

		using Knapsack_t     = Knapsack_<economy_norm_t>;
		using choice_index_t = typename Knapsack_t::choice_index_t;
		using Decision_t     = typename Knapsack_t::Decision;
		using Option_t       = typename Knapsack_t::Option;

		using Setting_t        = Setting_<economy_t>;
		using strategy_index_t = choice_index_t;

		static const choice_index_t NO_CHOICE = Knapsack_t::NO_CHOICE;

		/*
			A measurement...
				Measured burden should ALWAYS be >= 0.
		*/
		struct Measurement
		{
			burden_t         burden   = economy_t::impossible();
			choice_index_t   choice   = Knapsack_t::NO_CHOICE;
			//strategy_index_t strategy = Knapsack_t::CHOICE_NONE;

			bool valid() const    {return choice != Knapsack_t::NO_CHOICE;}
		};

		// Statistics on burdens.
		struct burden_stat_t
		{
			scalar_t _k  = 0;
			burden_t _mk = 0;
			burden_t _vk = 0;

			void reset()    {_k = 0; _mk = 0; _vk = 0;}

			explicit operator bool() const    {return _k > 0;}

			scalar_t count    () const    {return _k;}
			burden_t mean     () const    {return _mk;}
			burden_t variance () const    {return _vk / std::max<burden_t>(_k - 1, 1);}
			burden_t deviation() const    {return std::sqrt(variance());}

			burden_norm_t burden_norm() const    {return {mean(), variance()};}

			burden_t mean_plus_sigmas(scalar_t sigmas)    {return mean() + deviation() * sigmas;}

			void push(const burden_t burden)
			{
				burden_t d = (_k++ ? (burden - _mk) : 0);
				_mk += d / _k;             // First frame, add burden
				_vk += d * (burden - _mk); // First frame, add 0
			}

			// Decay methods for calculating *recent* variance.  0 < alpha < 1.
			void decay     (scalar_t alpha)
			{
				_k = 1 + (_k - 1) * alpha;
				_vk *= alpha;
			}
			void push_decay(const burden_t burden, scalar_t alpha)
			{
				_k *= alpha;
				burden_t d = (_k++ ? (burden - _mk) : 0);
				_mk += d / _k;
				_vk  = _vk * alpha + d * (burden - _mk);
			}
		};

		struct Estimate
		{
			burden_stat_t past_run;
			burden_stat_t this_run;
			burden_stat_t recent;
		};

		struct Estimates
		{
			size_t               data_count = 0;
			bool                 fully_explored = false;
			const choice_index_t count;
			Estimate             estimates[1];

			Estimates(choice_index_t option_count)    : count(option_count) {}

			static Estimates *alloc(choice_index_t count)
			{
				assert(count > 0);
				auto *e = new (new size_t[(sizeof(Estimates) + (count-1) * sizeof(Estimate) + sizeof(size_t)-1) / sizeof(size_t)]) Estimates(count);
				for (choice_index_t i = 0; i < count; ++i) new (e->estimates+i) Estimate();
				return e;
			}
			static void free(Estimates *e)    {delete[] reinterpret_cast<size_t*>(e);}
		};

		struct Config
		{
			float recent_alpha  = 1.f - 1.f/30.f;
			float measure_quota = 30;
			float pessimism_sd  = 1.0f;
		};

	public:
		Config config;

	private:
		Knapsack_t                                 _knapsack;
		std::unordered_map<std::string, Estimates*> estimates;
		std::unordered_map<Setting_t*, Decision_t>  settings;
		std::vector<Option_t>                       option_store;

		Estimates &estimates_for(const std::string &id, choice_index_t option_count)
		{
			auto i = estimates.find(id);
			if (i == estimates.end())
				i = estimates.emplace(id, Estimates::alloc(option_count)).first;
			assert(i->second->count == option_count);
			return *i->second;
		}

		struct Conversion
		{
			burden_t past = 0, present = 0;

			scalar_t ratio() const
			{
				return (past > 0 && present > 0) ? present / past : scalar_t(0);
			}
		}
			conversion;

	public:
		Goblin_();
		~Goblin_();

		/*
			Add & remove settings.
		*/
		bool add   (Setting_t *setting);
		void remove(Setting_t *setting);

		/*
			Update all settings, accounting for any new measurements.
		*/
		void update(burden_t capacity, size_t precision);

		/*
			Access the knapsack solver (for stats)
		*/
		const Knapsack_t &knapsack() const    {return _knapsack;}

		/*
			Get estimate data...
		*/
		const Estimates *get_estimates(std::string id) const
		{
			auto i = estimates.find(id);
			return (i==estimates.end()) ? nullptr : i->second;
		}
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
		while (settings.size()) remove(settings.begin()->first);
		for (auto &i : estimates) Estimates::free(i.second);
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
	void Goblin_<Econ>::update(burden_t capacity, size_t precision)
	{
		_knapsack.clear();
		_knapsack.economy.sigmas = config.pessimism_sd;
		option_store.clear();

		// TODO deal with lack of past-run profiling info

		// Harvest any new measurements
		for (auto &pair : settings)
		{
			auto *setting = pair.first;
			auto measure = setting->measurement();

			if (measure.valid())
			{
				auto &estimates = estimates_for(setting->id(), setting->options().option_count);
				assert(measure.choice < estimates.count);

				auto &estimate  = estimates.estimates[measure.choice];

				++estimates.data_count;
				if (estimate.this_run.count() >= config.measure_quota)
					estimate.recent.push_decay(measure.burden, config.recent_alpha);
				else estimate.recent.push(measure.burden);
				estimate.this_run.push      (measure.burden);

				if (estimate.past_run)
				{
					conversion.past    += estimate.past_run.mean();
					conversion.present += measure.burden;
				}
			}
		}

		// Calculate proportion between past-run costs and this-run costs.
		auto ratio = conversion.ratio();

		// Calculate estimated burden for all options and generate a knapsack problem
		for (auto &pair : settings)
		{
			auto *setting = pair.first;
			auto &decision = pair.second;
			const typename Setting::Options &options = setting->options();
			decision.option_count = options.option_count;

			auto &estimates = estimates_for(setting->id(), decision.option_count);

			// Estimate burdens for each choice.
			if (estimates.data_count == 0)
			{
				// Lacking any profiler data from this run, we force to the default choice.
				if (decision.choice >= decision.option_count) decision.choice = 0;
				for (choice_index_t i = 0; i < options.option_count; ++i)
				{
					option_store.push_back(Option_t{
						{(i == decision.choice) ? economy_t::trivial() : economy_t::impossible()},
						options.options[i].value});
				}
			}
			else
			{
				// Calculate a blind guess for unprofiled options?
				burden_norm_t blind_guess;
				choice_index_t untried_options = 0;
				if (!estimates.fully_explored)
				{
					burden_norm_t lightest = economy_norm_t::impossible();

					untried_options = decision.option_count;
					for (choice_index_t i = 0; i < decision.option_count; ++i)
					{
						auto &est = estimates.estimates[i];
						burden_norm_t test;
						if      (est.this_run) test = est.this_run.burden_norm();
						else if (est.past_run) test = est.past_run.burden_norm() * ratio;
						else    {++untried_options; continue;}
						if (_knapsack.economy.lesser(test, lightest)) lightest = test;
					}

					if (untried_options)
					{
						blind_guess = lightest * scalar_t(
							(config.measure_quota * untried_options) / estimates.data_count);
					}
					else estimates.fully_explored = true;
				}

				// Estimate burdens for each option, if possible.
				for (choice_index_t i = 0; i < decision.option_count; ++i)
				{
					burden_norm_t option_burden;
					auto &est = estimates.estimates[i];

					if (est.this_run)
					{
						if (est.this_run.count() < config.measure_quota)
						{
							// Calculate burdens for this run and prior assumptions.
							burden_norm_t current = est.this_run.burden_norm();
							burden_norm_t prior = (est.past_run ?
								(est.past_run.burden_norm() * ratio) :
								(blind_guess));

							// Interpolate between the two.
							float mix = est.this_run.count() / config.measure_quota;
							option_burden = current * mix + prior * (1.f-mix);
							//std::cout << " PM#" << decision.options[i].burden;
						}
						else
						{
							// Estimate based on recent measurements
							option_burden = est.recent.burden_norm();
							//std::cout << " M#" << decision.options[i].burden;
						}
					}
					else if (est.past_run)
					{
						// Estimate by comparing measurements from a past session.
						option_burden = est.past_run.burden_norm() * ratio;
						//std::cout << " P#" << decision.options[i].burden;
					}
					else
					{
						// This option is unexplored and inestimable.  Guess blindly.
						//std::cout << " #B";
						option_burden = blind_guess;

						// This option is unexplored and inestimable.  We'll guess it below.
						estimates.fully_explored = false;
						++untried_options;
					}

					option_store.push_back(Option_t{option_burden, options.options[i].value});
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

		// Finally, run the knapsack solver.
		_knapsack.decide({capacity, economy_t::trivial()}, precision);
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
