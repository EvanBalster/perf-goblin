#pragma once

/*
	Read & write profile data in JSON format.
*/

#include <iostream>
#include <cmath>
#include "profile.h"


#define PERF_GOBLIN_IO_DEBUG 1


namespace perf_goblin
{
	namespace detail
	{
		bool req_char(std::istream &i, const char required_char)
		{
			if (i.peek() == required_char) {i.get(); return true;}
#if PERF_GOBLIN_IO_DEBUG
			std::cerr << "req_char: expected `" << required_char << "', got `" << i.peek() << "'" << std::endl;
#endif
			i.setstate(std::ios_base::failbit);
			return false;
		}
		bool req_str(std::istream &i, const char *required_str)
		{
			while (*required_str)
				if (!req_char(i, *(required_str++))) return false;
			return true;
		}
		void skip_ws(std::istream &i)
		{
			while (true)
			{
				switch (i.peek())
				{
					case int(' ' ): case int('\n'): case int('\r'):
					case int('\t'): case int('\f'): case int('\v'):
						i.get();
						continue;
				}
				break;
			}
		}
		bool req_char_ws(std::istream &i, const char required_char)
		{
			skip_ws(i);
			return req_char(i, required_char);
		}
		bool req_chars_ws(std::istream &i, const char *required_chars)
		{
			while (*required_chars)
				{skip_ws(i); if (!req_char(i, *(required_chars++))) return false;}
			return true;
		}
	}

	/*
		Write/read normal burdens as JSON.
			We store standard deviation rather than variance, for precision reasons.
	*/
	/*template<typename T_Base>
	std::ostream &operator<<(std::ostream &o, const typename Economy_Normal_<T_Base>::burden_t &burden)
	{
		return o << '[' << burden.mean << ',' << std::sqrt(burden.var) << ']';
	}
	template<typename T_Base>
	std::istream &operator>>(std::istream &i, typename Economy_Normal_<T_Base>::burden_t &burden)
	{
		if (i.good()) do
		{
			typename Economy_Normal_<T_Base>::base_burden_t in_m, in_d;
			if (!detail::req_char(i,'[') || (i >> in_m).fail()) continue;
			if (!detail::req_char(i,',') || (i >> in_d).fail()) continue;
			if (!detail::req_char(i,']'))                       continue;
			burden.mean = in_m;
			burden.var = in_d*in_d;
		}
		while (0);
		return i;
	}*/

	/*
		Read/write burden statistics as JSON.
	*/
	template<typename T_Econ>
	std::ostream &operator<<(std::ostream &o, const typename BurdenStat_<T_Econ> &stat)
	{
		int n = stat.count();
		o << '[';
		if (stat.count() == n) o << n;
		else                   o << stat.count();
		return o << ',' << stat.mean() << ',' << stat.deviation() << ']';
	}
	template<typename T_Econ>
	std::istream &operator>>(std::istream &i, typename BurdenStat_<T_Econ> &stat)
	{
		if (i.good()) do
		{
			typename Profile_<T_Econ>::scalar_t in_n;
			typename Profile_<T_Econ>::burden_t in_m, in_d;
			if (!detail::req_char_ws(i,'[') || (i >> in_n).fail()) continue;
			if (!detail::req_char_ws(i,',') || (i >> in_m).fail()) continue;
			if (!detail::req_char_ws(i,',') || (i >> in_d).fail()) continue;
			if (!detail::req_char_ws(i,']'))                       continue;
			stat._k = in_n;
			stat._mk = in_m;
			stat._vk = (in_d*in_d) * (in_n-1);
		}
		while (0);
		return i;
	}

	/*
		Read/write profile as JSON.
	*/
	template<typename T_Econ>
	std::ostream &operator<<(std::ostream &o, const Profile_<T_Econ> &profile)
	{
		o << "{";
		bool first = true;
		for (auto entry : profile)
		{
			if (first) first = false;
			else       o << ',';
			auto &task = *entry.second;
			o << "\n\t\"" << entry.first << "\":[";
			for (size_t i = 0, last = task.count-1; i <= last; ++i)
				perf_goblin::operator<<(o, task.estimates[i].full) << (",]"[i == last]);
		}
		return o << "\n}";
	}
	template<typename T_Econ>
	std::istream &operator>>(std::istream &i, Profile_<T_Econ> &profile)
	{
		typename Profile_<T_Econ>::Task *task_data = nullptr;

		static const typename Profile_<T_Econ>::choice_index_t MAX_OPTIONS = 1024;

		if (i.good()) do
		{
			if (!detail::req_char_ws(i,'{')) continue;
			profile.clear();

			task_data = typename Profile_<T_Econ>::Task::alloc(MAX_OPTIONS);

			while (true)
			{
				detail::skip_ws(i);

				if (!detail::req_char_ws(i,'"')) break;
				std::string id;
				while (true)
				{
					char c = i.get();
					if (c == '"') break;
					if (!i.good())
					{
#if PERF_GOBLIN_IO_DEBUG
						std::cerr << "profile load: stream failure while reading id" << std::endl;
#endif
						goto parse_failure;
					}
					if (c == '\r' || c == '\n' || c == '\v' || c == '\f' || c == '\0')
					{
#if PERF_GOBLIN_IO_DEBUG
						std::cerr << "profile load: id cannot contain char #" << int(c) << std::endl;
#endif
						goto parse_failure;
					}
					id.push_back(c);
				}

				// Read estimate array
				if (!detail::req_chars_ws(i,":[")) break;
				typename Profile_<T_Econ>::choice_index_t estimate_count = 0;
				while (estimate_count < MAX_OPTIONS)
				{
					// Read an estimate
					BurdenStat_<T_Econ> &stat = task_data->estimates[estimate_count].full;
					if (!(i >> stat).good())
					{
#if PERF_GOBLIN_IO_DEBUG
						std::cerr << "profile load: failed to read burden stats" << std::endl;
#endif
						goto parse_failure;
					}
					++estimate_count;

					// Comma or closing bracket
					detail::skip_ws(i);
					char c = i.get();
					if (c == ',') continue;
					if (c == ']') break;
#if PERF_GOBLIN_IO_DEBUG
					std::cerr << "profile load: expected `,' or `]', got `" << c << "'" << std::endl;
#endif
					goto parse_failure;
				}

				// Assimilate the loaded profile data for the task.
				const_cast<typename Profile_<T_Econ>::choice_index_t&>(task_data->count) = estimate_count;
				profile.assimilate(id, *task_data);
				
				// Comma or closing brace
				detail::skip_ws(i);
				char c = i.get();
				if (c == ',') continue;
				if (c == '}') break;
#if PERF_GOBLIN_IO_DEBUG
				std::cerr << "profile load: expected `,' or `}', got `" << c << "'" << std::endl;
#endif
				goto parse_failure;
			}

			break;
		}
		while (0);
	
		goto finish;

	parse_failure:
		i.setstate(std::ios_base::failbit);
	finish:
		if (task_data)
		{
			const_cast<typename Profile_<T_Econ>::choice_index_t&>(task_data->count) = MAX_OPTIONS;
			typename Profile_<T_Econ>::Task::free(task_data);
		}
		return i;
	}
}
