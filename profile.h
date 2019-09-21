#pragma once

#include <unordered_map> // Goblin's estimate and setting maps
#include <string>        // Used to classify profiled items.
#include <cstdint>

#include "economy.h"

/*
	A Profile aggregates performance data for various tasks,
		which are categorized by string identifiers.
		Each task has a fixed number of optional alternatives.
*/

namespace perf_goblin
{
	template<typename T_Economy> class Profile_;
	using Profile_f = Profile_<Economy_f>;

	template<typename T_Economy>
	class Profile_
	{
	public:
		using economy_t      = T_Economy;
		using burden_t       = typename economy_t::burden_t;
		using scalar_t       = typename economy_t::scalar_t;
		using value_t        = typename economy_t::value_t;

		using economy_norm_t = Economy_Normal_<economy_t>;
		using burden_norm_t  = typename economy_norm_t::burden_t;
		using capacity_t     = typename economy_norm_t::capacity_t;

		using choice_index_t = uint16_t;
		static const choice_index_t NO_CHOICE = ~choice_index_t(0);

		/*
			A measurement...
				Measured burden should ALWAYS be >= 0.
		*/
		struct Measurement
		{
			burden_t         burden   = economy_t::infinite();
			choice_index_t   choice   = NO_CHOICE;
			//strategy_index_t strategy = Knapsack_t::CHOICE_NONE;

			bool valid() const    {return choice != NO_CHOICE;}
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
			burden_t sum      () const    {return _k*_mk;}
			burden_t mean     () const    {return _mk;}
			burden_t variance () const    {return _vk / std::max<burden_t>(_k - 1, 1);}
			burden_t deviation() const    {return std::sqrt(variance());}

			burden_norm_t burden_norm() const    {return {mean(), variance()};}
			void make_certain(const burden_norm_t burden)    {_k = 1e10f; _mk = burden.mean; _vk = burden.var*_k;}

			burden_t mean_plus_sigmas(scalar_t sigmas)    {return mean() + deviation() * sigmas;}

			void push(const burden_t burden)
			{
				burden_t dm = (burden - _mk), dv = (_k++ ? dm : 0);
				_mk += dm / _k;             // First frame, add burden
				_vk += dv * (burden - _mk); // First frame, add 0
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
				burden_t dm = (burden - _mk), dv = (_k++ ? dm : 0);
				_mk += dm / _k;
				_vk  = _vk * alpha + dv * (burden - _mk);
			}

			// Scale this stat's mean and deviation by a factor
			void scale(const scalar_t scale_factor)
			{
				_mk *= scale_factor;
				_vk *= scale_factor*scale_factor;
			}

			// Pool data with another burden_stat_t
			burden_stat_t pool(const burden_stat_t &o) const
			{
				scalar_t net_count = count() + o.count();
				burden_t net_mean = (o.sum() + sum()) / net_count;
				burden_t diff = o.mean() - mean();
				// Unbiased variance combination formula (O'Neill 2014)
				burden_t net_vk = o._vk + _vk +
					diff*diff * (count()*o.count()) / net_count;

				return burden_stat_t{net_count, net_mean, net_vk};
			}
		};

		/*
			An estimate for a task option.
		*/
		struct Estimate
		{
			burden_stat_t full;
			burden_stat_t recent;

			explicit operator bool() const    {return bool(full);}
		};

		struct Task
		{
			size_t               data_count = 0;
			mutable bool         fully_explored = false; // flag, used by goblin
			const choice_index_t count;
			Estimate             estimates[1];

			Task(choice_index_t option_count)    : count(option_count) {}
			Task(const Task &o)                  : count(o.count) {*this = o;}

			// Copyable.
			Task& operator=(const Task &o)
			{
				assert(count == o.count);
				data_count = o.data_count;
				fully_explored = o.fully_explored;
				for (choice_index_t i = 0; i < count; ++i) estimates[i] = o.estimates[i];
				return *this;
			}

			// Iterable.
			const Estimate *begin() const    {return estimates;}
			const Estimate *end  () const    {return estimates+count;}
			Estimate       *begin()          {return estimates;}
			Estimate       *end  ()          {return estimates+count;}

			// Allocate / deallocate.
			static Task *alloc(choice_index_t count)
			{
				assert(count > 0);
				auto *e = new (new size_t[(sizeof(Task) + (count-1) * sizeof(Estimate) + sizeof(size_t)-1) / sizeof(size_t)]) Task(count);
				for (choice_index_t i = 0; i < count; ++i) new (e->estimates+i) Estimate();
				return e;
			}
			static void free(const Task *e)    {delete[] reinterpret_cast<const size_t*>(e);}
		};

		using Tasks = std::unordered_map<std::string, const Task*>;

	protected:
		Tasks _tasks;

		Task &task_init(const std::string &id, choice_index_t option_count)
		{
			const Task *task = find(id);
			if (!task) _tasks[id] = task = Task::alloc(option_count);
			assert(task->count == option_count);
			return *const_cast<Task*>(task);
		}

	public:
		Profile_ ()                     {}
		Profile_ (const Profile_ &o)    {*this = o;}
		~Profile_()                     {clear();}

		/*
			Get profile data for a task, if available.
		*/
		const Task *find(std::string id) const
		{
			auto i = _tasks.find(id);
			return (i == _tasks.end()) ? 0 : i->second;
		}

		/*
			Add a measurement for a task.
		*/
		const Task *collect(const std::string &id, choice_index_t option_count, const Measurement &measurement)
		{
			if (!measurement.valid()) return nullptr;
			Task     &task     = task_init(id, option_count);
			Estimate &estimate = task.estimates[measurement.choice];
			++task.data_count;
			estimate.recent.push(measurement.burden);
			estimate.full  .push(measurement.burden);
			return &task;
		}

		/*
			Assimilate "full" profile data from some other task, with a scaling factor.
		*/
		const Task *assimilate(const std::string &id, const Task &data, const scalar_t scale_factor = 1)
		{
			Task &task = task_init(id, data.count);
			for (choice_index_t i = 0; i < task.count; ++i)
			{
				auto &est = task.estimates[i];
				burden_stat_t scaled = data.estimates[i].full;
				scaled.scale(scale_factor);
				est.full = est.full.pool(scaled);
			}
			return &task;
		}

#if 0
		// Provide perfect knowledge of a burden's distribution (debug)
		void make_certain(std::string id, choice_index_t option_index, choice_index_t option_count, burden_norm_t burden)
		{
			task_init(id, option_count).estimates[option_index].this_run.make_certain(burden);
		}
#endif

		/*
			Subject recent measurements to an exponential decay.
				Decay does not immediately affect mean or variance, only count.
				Diminishes the influence of previous measurements.
		*/
		void decay_recent(scalar_t alpha)
		{
			for (auto &task : _tasks)
				for (auto &estimate : *const_cast<Task*>(task.second))
					estimate.recent.decay(alpha);
		}

		/*
			Access the set of known tasks.
		*/
		const Tasks &tasks() const    {return _tasks;}

		/*
			Clear/copy
		*/
		Profile_ &operator=(const Profile_ &o)
		{
			clear();
			for (auto &i : o._tasks) task_init(i.first, i.second->count) = *i.second;
			return *this;
		}
		void clear()    {for (auto &i : _tasks) Task::free(i.second); _tasks.clear();}
	};
}