#pragma once


/*
	The knapsack algorithms in this library use rulesets called "economies".

	An economy describes a burden and a value.
		The value is a simple number that we want to maximize.
		The burden represents one or more limited resources.

	Economy_f, the most common, uses a float burden and float value.
*/

namespace perf_goblin
{
	template<typename T_Burden, typename T_Value = float> struct Economy_;
	template<typename T_BaseEconomy>                      struct Economy_Normal_;

	using Economy_f        = Economy_<float, float>;
	using Economy_Normal_f = Economy_Normal_<Economy_f>;

	/*
		Utility: concepts of zero and infinity
	*/
	namespace detail
	{
		struct Zero_t     {template<typename T> operator T() const {return 0;}};
		struct Infinity_t {template<typename T> operator T() const {return std::numeric_limits<T>::infinity();}};
	}

	/*
		Class for basic economies with a scalar burden.
	*/
	template<
		typename T_Burden,
		typename T_Value /* defaults to float */>
	struct Economy_
	{
		static const bool burden_is_scalar = true;
		using burden_t   = T_Burden;
		using value_t    = T_Value;

		// capacity_t is the same as burden_t in simple economies.
		using capacity_t = burden_t;

		// scalar_t is 
		using scalar_t   = T_Burden;

		// Concepts of zero and infinity.
		static detail::Zero_t     zero()        {return {};}
		static detail::Infinity_t infinite()    {return {};}

		// Is a burden possible to bear?
		static constexpr bool is_possible(const burden_t burden)    {return acceptable(burden, infinite());}

		// Return whether one burden is strictly less than another.
		//   Probabilistic economies 
		static bool lesser(const burden_t &lhs, const burden_t &rhs)    {return (lhs < rhs);}

		// Return whether the burden is possible within the capacity
		static bool acceptable(const burden_t &lhs, const capacity_t &rhs)    {return (lhs < rhs);}
	};


	/*
		An economy for normally-distributed burdens.
			This allows us to consolidate independent variations in cost.
	*/
	template<typename T_BaseEconomy>
	struct Economy_Normal_
	{
	public:
		using base_t        = T_BaseEconomy;
		using base_burden_t = typename base_t::burden_t;
		using base_capac_t  = typename base_t::capacity_t;
		using value_t       = typename base_t::value_t;
		using scalar_t      = typename base_t::scalar_t;

		struct burden_t
		{
			base_burden_t mean;
			base_burden_t var;
			
			// TEMPORARY
			operator base_burden_t() const    {return mean;}

			burden_t  operator* (const scalar_t  s) const    {return {mean*s, var*(s*s)};}
			burden_t &operator*=(const scalar_t  s)          {mean *= s; var *= s*s; return *this;}
			burden_t  operator/ (const scalar_t  s) const    {return {mean/s, var/(s*s)};}
			burden_t &operator/=(const scalar_t  s)          {mean /= s; var /= s*s; return *this;}
			burden_t  operator+ (const burden_t &o) const    {return {mean+o.mean, var+o.var};}
			burden_t &operator+=(const burden_t &o)          {mean += o.mean; var += o.var; return *this;}
			burden_t  operator- (const burden_t &o) const    {return {mean-o.mean, var+o.var};}
			burden_t &operator-=(const burden_t &o)          {mean -= o.mean; var += o.var; return *this;}

			base_burden_t sigma_offset(const scalar_t sigmas) const
			{
				return mean + sigmas * std::sqrt(var);
			}
		};

		/*
			A maximum capacity that applies to mean net burden,
				plus some number of sigmas (standard deviations).
				3-5 sigmas makes exceeding capacity very unlikely.
		*/
		struct capacity_t
		{
			base_capac_t limit;
			scalar_t     sigmas = 3;
		};

	public:
		static const bool burden_is_scalar = base_t::burden_is_scalar;

		struct Zero_t     : public detail::Zero_t        {operator burden_t() const {return {base_t::zero(),     base_t::zero()};}};
		struct Infinity_t : public detail::Infinity_t    {operator burden_t() const {return {base_t::infinite(), base_t::zero()};}};

		static Zero_t     zero()        {return {};}
		static Infinity_t infinite()    {return {};}
		
		static constexpr bool is_possible(const burden_t burden)    {return base_t::is_possible(burden.mean) & base_t::is_possible(burden.var);}

		/*
			There isn't an objective total order for normal burdens.
				For knapsack purposes, we order by mean...
		*/
		static bool lesser(const burden_t &lhs, const burden_t &rhs)
		{
			// Strictly comparing means...
			return base_t::lesser(lhs.mean, rhs.mean);
			/*base_burden_t diff = (lhs.mean - rhs.mean);
			diff = (diff*diff) / (sigmas*sigmas);
			return base_t::lesser(diff, rhs.var + lhs.var - scalar_t(2)*std::sqrt(lhs.var*rhs.var));*/
		}

		static bool acceptable(const burden_t &burden, const capacity_t &capac)
		{
#if 0
			assert(burden.var == 0);
			return base_t::acceptable(burden.mean, capac.limit);
#else
			// (mean + E*sqrt(var)) < limit  ==>  E^2*var < (limit-mean)^2
			if (!base_t::acceptable(burden.mean, capac.limit)) return false;
			base_burden_t margin = capac.limit - burden.mean;
			return base_t::lesser(capac.sigmas*capac.sigmas * burden.var, margin*margin);
#endif
		}
	};
}