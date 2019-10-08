#pragma once

#include "goblin.h"


namespace perf_goblin
{
	/*
		Utility class for a setting with a fixed-size option array.
	*/
	template<typename T_Economy, uint16_t T_OptionCount>
	class Setting_Array_ : public Setting_<T_Economy>
	{
	public:
		static_assert(T_OptionCount > 0);

		static const uint16_t option_count = T_OptionCount;

		using setting_t        = Setting_<T_Economy>;
		using Option           = typename setting_t::Option;
		using Options          = typename setting_t::Options;
		using Measurement      = typename setting_t::Measurement;
		using choice_index_t   = typename setting_t::choice_index_t;
		using strategy_index_t = typename setting_t::choice_index_t;

		using economy_t        = T_Economy;
		using burden_t         = typename economy_t::burden_t;
		using value_t          = typename economy_t::value_t;

	protected:
		std::string _id;
		Option      _option_array[option_count];
		Options     _options;
		uint16_t    _choice_default;
		uint16_t    _choice_current;
		Measurement _measurement;

	public:
		Setting_Array_(
			std::string id,
			Option      option_array[option_count],
			uint16_t    choice_default = 0) :
				_id(id), _options{option_array, option_count},
				_choice_default(choice_default), _choice_current(choice_default)
		{
			for (uint16_t i = 0; i < option_count; ++i) _option_array[i] = option_array[i];
		}

		~Setting_Array_() override {}

		const std::string &id()         const final            {return _id;}
		const Options &options()        const final    {return _options;}
		choice_index_t choice_default() const final    {return _choice_default;}

		// These methods facilitate using this class without extending it.
		choice_index_t choice_current() const         {return _choice_current;}
		void measurement_set(const Measurement &m)    {_measurement = m;}

	protected:
		// These methods may be overridden in a deriving class.
		void           choice_set(
			choice_index_t   choice_index,
			strategy_index_t strategy_index) override
		{
			_choice_current = choice_index;
		}
		Measurement measurement() override
		{
			auto m = _measurement;
			_measurement = Measurement();
			return m;
		}
	};

	/*
		Special cases of the above.
	*/
	template<typename T_Econ> using Setting_Fixed_  = Setting_Array_<T_Econ, 1>;
	template<typename T_Econ> using Setting_Binary_ = Setting_Array_<T_Econ, 2>;

	/*
		Create a non-changeable setting.
			Useful for unavoidable burdens and fixed incentives.
	*/
	template<typename T_Econ>
	Setting_Fixed_<T_Econ> Create_Setting_Fixed_(
		const       std::string &id,
		typename T_Econ::value_t value)
	{
		return new Setting_Fixed_<T_Econ>(id, {{value}}, 0);
	}

	/*
		Create an on/off setting.
	*/
	template<typename T_Econ>
	Setting_Binary_<T_Econ> Create_Setting_OnOff_(
		const       std::string &id,
		typename T_Econ::value_t value_on,
		typename T_Econ::value_t value_off = 0,
		bool                     default_on = false)
	{
		return new Setting_Binary_<T_Econ>(id, {{value_off},{value_on}}, default_on);
	}

	/*
		Create a multiple-choice setting.
	*/
	template<typename T_Econ, uint16_t N>
	Setting_Binary_<T_Econ> Create_Setting_(
		const                std::string &id,
		typename Setting_<T_Econ>::Option option_array[N],
		uint16_t                          default_choice = 0)
	{
		return new Setting_Binary_<T_Econ>(id, option_array, default_choice);
	}
}
