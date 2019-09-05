#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cmath>
#include <ctime>
#include <chrono>
#include <random>

#include "knapsack.h"
#include "goblin.h"


using namespace perf_goblin;

using std::cout;
using std::endl;


static std::random_device rand_dev;
static std::mt19937       random;


std::ostream& operator<<(std::ostream &out, const Knapsack::Option &option)
{
	return out << "(#" << option.burden << " $" << option.value << ')';
}

std::ostream& operator<<(std::ostream &out, const Knapsack::Decision &decision)
{
	out << '{';
	for (size_t i = 0; i < decision.option_count; ++i)
	{
		out << (i ? ", " : "") << decision.options[i];
	}
	out << '}';
	return out;
}

std::ostream& operator<<(std::ostream &out, const Knapsack::Stats &stats)
{
	return out << "(#" << stats.net_burden << " $" << stats.net_value << " @" << stats.net_score << ")";
}

static float random_burden()
{
	unsigned rv = random();
	return .2f + .8f * + ((rv & 255u) / 255.f) * (((rv >> 8u) & 255u) / 2.55f);
}

static float random_value(float burden)
{
	unsigned rv = random();
	float uncorrelated_val = ((rv & 255u) / 255.f) * (((rv >> 8u) & 255u) / 2.55f);
	return std::sqrt(burden * uncorrelated_val);
}

static float random_capacity(size_t decisions)
{
	float capacity = 0.f;
	size_t N = decisions;
	for (size_t i = N/2; i < N; ++i) capacity += random_burden();
	return capacity;
}

void write_svg(std::ostream &out, Knapsack &problem, float max_burden)
{
	out << std::fixed << std::setprecision(3);

	float svg_width = 600.0f, svg_height = 200.0f;

	out << "<svg width=\"" << svg_width << "\" height=\"" << svg_height << "\">\n";

	// Capacity
	out << "\t<g stroke=\"black\" fill=\"darkgray\" stroke-width=\"0\">\n";
	out << "\t<rect x=\"30\" y=\"150\" width=\"540\" height=\"20\" fill=\"darkgray\"/>\n";
	out << "\t<rect x=\"30\" y=\"50\" width=\"20\" height=\"120\" fill=\"darkgray\"/>\n";
	out << "\t<rect x=\"550\" y=\"50\" width=\"20\" height=\"120\" fill=\"darkgray\"/>\n";
	out << "\t</g>\n";

	out << "\t<g stroke=\"black\" fill=\"gray\" stroke-width=\".5\">\n";

	float x = 50.f, x_scale = 500.f / max_burden, y_scale = 100.f / 4.f;

	struct box_t
	{
		float burden, value_burden_ratio;
		size_t index;
	};
	std::vector<box_t> boxes;

	for (unsigned i = 0; i < problem.decisions.size(); ++i)
	{
		auto *d = problem.decisions[i];
		auto &c = d->chosen();
		if (c.burden <= 0.f) continue;

		box_t box = {c.burden, c.value / c.burden, i};
		boxes.push_back(box);
	}
	std::sort(boxes.begin(), boxes.end(), [](const box_t &l, const box_t &r) -> bool {return l.value_burden_ratio > r.value_burden_ratio;});

	for (auto &box : boxes)
	{
		auto *d = problem.decisions[box.index];
		auto &c = d->chosen();

		float y = 150.f;
		float wid = x_scale * box.burden, hei = y_scale * box.value_burden_ratio;

		if (c.value == 0.f) {y += 10.f; hei = 10.f;}

		out << "\t\t<rect x=\"" << x << "\" y=\"" << (y-hei)
			<< "\" width=\"" << wid << "\" height=\"" << hei;
		if (c.value == 0.f) out << "\" fill=\"#CC6666";
		else if (d->option_count == 2) out << "\" fill=\"#55BBBB";
		out << "\"/>";
		out << "<!-- " << c << " -->" "\n";
		x += wid;
	}

	out << "\t</g>\n";

	out << "</svg>";
}

void describe_problem(std::ostream &out, Knapsack &problem)
{
	out << "problem & solution:" << endl;
	for (unsigned i = 0; i < problem.decisions.size(); ++i)
	{
		auto &decision = problem.decisions[i];
		out << " " << std::setw(3) << (i+1) << ": ";
		auto opts = decision->option_count;
		switch (opts)
		{
		case 0: case 1: out << "   ";                       break;
		case 2:  out << (decision->choice ? " on" : "off");  break;
		default: out << (decision->choice+1) << "/" << opts; break;
		}
		
		//<< " " << decision.chosen()
		//<< " @" << std::setw(2) << decision.chosen().score << endl
		out << " ~ " << *decision << endl;
	}
	out << endl;

	/*out << "solution:" << endl;
	for (auto &decision : problem.decisions)
	{
		cout << "  d" << std::setw(3) << (&decision-&problem.decisions[0] + 1)
			
	}*/
}

void generate_decision(Knapsack::Decision &decision, std::vector<Knapsack::Option> &options)
{
	switch (random() & 7)
	{
	case 0:
		// Fixed burden
		decision.option_count = 1;
		options.push_back(Knapsack::Option{random_burden()});
		break;

	case 1:
		// Fixed incentive
		decision.option_count = 1;
		options.push_back(Knapsack::Option{0, random_value(random_burden()) - random_value(random_burden())});
		break;
			
	case 2: case 3: case 4:
		// Binary choice
		{
			float burden = random_burden(), value = random_value(burden);

			decision.option_count = 2;
			options.push_back(Knapsack::Option{0,0});
			options.push_back(Knapsack::Option{burden, value});
		}
		break;

	case 5: case 6:
		// Multiple choice, orderly
		{
			float burden = 0.f, value = 0.f;
			unsigned count = 2u + (1u + (random() & 3u)) * (1u + (random() & 7u));
			for (unsigned i = 0; i < count; ++i)
			{
				float new_burden = random_burden() * (2.f/count);
				burden += new_burden;
				value += random_value(new_burden);
				Knapsack::Option option = {burden, value};
				options.push_back(option);
				++decision.option_count;
			}
		}
		break;

	case 7: default:
		// Multiple choice, chaotic
		{
			unsigned count = 2u + (1u + (random() & 3u)) * (1u + (random() & 7u));
			for (unsigned i = 0; i < count; ++i)
			{
				float burden = random_burden() * 2.f;
				float value = random_value(burden);
				Knapsack::Option option = {burden, value};
				options.push_back(option);
				++decision.option_count;
			}
		}
		break;
	}

	decision.options = &options[options.size() - decision.option_count];
}

void generate_problem(Knapsack &knapsack, size_t count = 50)
{
	static std::vector<Knapsack::Decision> decisions;
	static std::vector<Knapsack::Option>   options;

	decisions.clear();
	options.clear();

	// Generate a list of decisions
	for (unsigned i = 0; i < count; ++i)
	{
		decisions.emplace_back(Knapsack::Decision());
		generate_decision(decisions.back(), options);
	}

	// Set the option-list pointers
	Knapsack::Option *option = options.data();
	for (auto &d : decisions)
	{
		d.options = option;
		option += d.option_count;
	}

	// Formulate a knapsack problem
	knapsack.decisions.clear();
	for (auto &d : decisions)
	{
		knapsack.add_decision(&d);
	}
}

void test_knapsack()
{
	Knapsack problem;

	srand(unsigned(time(NULL)));

	cout << std::fixed << std::setprecision(1);

	while (true)
	{
		cout << "Generating a new multiple-choice knapsack problem." << endl;
		generate_problem(problem);
		cout << endl;

		cout << "  problem:" << endl;

		unsigned precision = 30;
		float max_burden = random_capacity(problem.decisions.size());

		{
			size_t total_options = 0;
			for (auto *d : problem.decisions) total_options += d->option_count;

			cout << "    decisions:     " << problem.decisions.size() << endl;
			cout << "    total options: " << total_options << endl;
			cout << "    mean opt/dec:  " << (float(total_options)/problem.decisions.size()) << endl;

			cout << "    burden limit:  #" << max_burden << endl;
			cout << "    precision:     " << precision << endl;
			cout << "    worst-case:    " << (precision * problem.decisions.size() * total_options) << " iterations" << endl;
			cout << endl;
		}

		// Run solver
		cout << "    (...solving...)" << endl;
		auto prof_time = std::chrono::high_resolution_clock::now();
		bool successful = problem.decide(max_burden, precision);
		float time_taken = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - prof_time).count();
		cout << endl;

		{
			size_t table_size = problem.minimums.store.size(),
				table_size_max = problem.decisions.size() * problem.stats.highest.net_score;

			/*for (auto &item : problem.minimums.store)
			{
				if (!(item.net_burden < Knapsack::economy_t::impossible())) ++table_empties;
			}*/

			cout << "  solver data:" << endl;
			cout << "    solver time: " << (1000000.f*time_taken) << " us" << endl;
			cout << "    solution is: ";
			if      (problem.stats.iterations) cout << "approximate (" << problem.stats.iterations << " iterations)";
			else if (successful)               cout << "ideal";
			else                               cout << "impossible (selecting minimum burden)";
			cout << endl;
			if (table_size)
			{
				cout << "    table size:  " << table_size << endl;
				cout << "    table fill:  " << (100.f*table_size / table_size_max) << "%" << endl;
			}
			cout << endl;
		}

		{
			cout << "  solution stats:" << endl;
			cout << "    min-burden: " << problem.stats.lightest << endl;
			cout << "    max-score:  " << problem.stats.highest << endl;
			cout << "    chosen:     " << problem.stats.chosen << endl;
			cout << "    efficiency: "
				<< "(#" << (100.f*problem.stats.chosen.net_burden / problem.stats.highest.net_burden) << "%"
				<< " $" << (100.f*problem.stats.chosen.net_value  / problem.stats.highest.net_value ) << "%"
				<< " @" << (100.f*problem.stats.chosen.net_score / problem.stats.highest.net_score) << "%) compared to max-score" << endl;
			cout << endl;
		}

		

		while (true)
		{
			cout << "What now?\n"
				"  R = go again (default action)\n"
				"  V = view problem and solution\n"
				"  S = save SVG diagram\n"
				"  Q = quit\n"
				">> ";
			std::string s;
			std::getline(std::cin, s);

			if (s.length() == 0 || s[0] == 'r' || s[0] == 'R' || s[0] == '\r' || s[0] == '\n')
			{
				break;
			}
			else if (s[0] == 'v' || s[0] == 'V')
			{
				describe_problem(cout, problem);
			}
			else if (s[0] == 'q' || s[0] == 'Q')
			{
				return;
			}
			else if (s[0] == 's' || s[0] == 'S')
			{
				std::ofstream out("solution.svg");
				write_svg(out, problem, max_burden);
				if (out.good()) cout << "saved to solution.svg" << endl;
				else            cout << "couldn't save to solution.svg" << endl;
			}
			else
			{
				cout << "unknown command." << endl;
			}
		}
	}
}

class SimSetting : public Setting
{
public:
	choice_index_t choice_index = 0;

	std::vector<Option> optionVec;
	Options            _options;

	std::vector<std::normal_distribution<float>> costs;

	unsigned random_steps = 1;

	Measurement measure;

	std::string _id;

	SimSetting()
	{
		Knapsack_<economy_t>::Decision tmp_decision;
		std::vector<Knapsack_<economy_t>::Option> tmp_options;
		generate_decision(tmp_decision, tmp_options);

		// Cost generators
		std::uniform_real_distribution<float> exp_range{std::log(1.01f), std::log(2.5f)};
		for (auto &option : tmp_options)
		{
			optionVec.push_back({option.value});
			costs.emplace_back(
				std::log(std::max(option.burden, 1e-20f)),
				exp_range(random));
		}
		_options.options = optionVec.data();
		_options.option_count = Goblin::choice_index_t(optionVec.size());

		// Generate ID
		for (unsigned i = 0; i < 12; ++i)
		{
			_id.push_back('a' + (random()%26));
		}
	}

	virtual choice_index_t choice_default() const override
	{
		return choice_index;
	}
	void choice_set(
		choice_index_t   _choice_index,
		strategy_index_t strategy_index) override
	{
		choice_index = _choice_index;
	}

	const Options &options() override
	{
		return _options;
	}
	const Option &chosen()
	{
		return _options.options[choice_index];
	}

	Measurement measurement() override
	{
		Measurement m = measure;
		measure.choice = NO_CHOICE;
		//if (m.valid())
		//	cout << "Prf[" << _id << "]: " << m.choice << " #" << m.burden << endl;
		return m;
	}

	void update()
	{
		measure.burden = std::exp(costs[choice_index](random));
		measure.choice = choice_index;
	}

	const std::string &id() const override    {return _id;}

	float expect_mean(unsigned option_index)
	{
		auto &o = costs[option_index];
		return std::exp(o.mean() + .5f*o.stddev()*o.stddev());
	}
};

void test_goblin()
{
	while (true)
	{
		Goblin goblin;

		auto &knapsack = goblin.knapsack();

		std::list<SimSetting> scenario;

		cout << "Generating a new performance control scenario." << endl;

		for (size_t i = 0; i < 50; ++i)
		{
			scenario.emplace_back();
			goblin.add(&scenario.back());
		}

		float capacity = random_capacity(scenario.size());
		size_t precision = 30;

		size_t option_count = 0;
		for (auto &s : scenario) option_count += s.options().option_count;

		{
			cout << "  capacity:     #" << capacity << endl;
			cout << "  precision:     " << precision << endl;
			cout << "  settings:      " << scenario.size()
				<< ", totaling " << option_count << " options" << endl;
			cout << "  measure quota: " << goblin.config.measure_quota << endl;
		}

		float knowledge_max = option_count * goblin.config.measure_quota;

		cout << "Running simulation..." << endl;
		size_t frames = 0, frames_overload = 0;
		SimSetting::burden_t load_total = {}, load_pess = {}, high_load_total = {}, light_load_total = {};
		SimSetting::value_t value_total = 0.f;

		for (size_t s = 0; s <= 14; ++s)
		{
			unsigned explored_count = 0;
			float knowledge_count = 0;

			size_t frame_quota = size_t(1) << s;

			while (frames < frame_quota)
			{
				++frames;

				// Update goblin
				goblin.update(capacity, precision);

				// Update all settings...
				for (auto &setting : scenario) setting.update();

				// Calculate stats for this frame
				SimSetting::burden_t net_cost = 0.f;
				SimSetting::value_t  net_value = 0.f;
				knowledge_count = 0;
				explored_count = 0;
				for (auto &setting : scenario)
				{
					net_cost += setting.measure.burden;
					net_value += setting.chosen().value;

					if (auto est = goblin.get_estimates(setting.id()))
					{
						if (est->fully_explored) ++explored_count;

						for (unsigned i = 0; i < est->count; ++i)
							knowledge_count += std::min(
								est->estimates[i].this_run.count(),
								goblin.config.measure_quota);
					}
				}

				// Accumulate stats
				load_total += net_cost;
				load_pess += knapsack.stats.chosen.net_burden.sigma_offset(knapsack.economy.sigmas);
				high_load_total += knapsack.stats.highest.net_burden.sigma_offset(knapsack.economy.sigmas);
				light_load_total += knapsack.stats.lightest.net_burden.sigma_offset(knapsack.economy.sigmas);
				value_total += net_value;
				if (net_cost > capacity) ++frames_overload;
			}

			size_t total_data = 0;
			for (auto &s : scenario)
			{
				auto *est = goblin.get_estimates(s.id());
				if (est) total_data += est->data_count;
			}

			cout << "  after " << frames << " frames:" << endl;
			cout << "     over-budget:    " << frames_overload << "/" << frames << " frames" << endl;
			cout << "     data collected: " << total_data << " measurements" << endl;
			cout << "     profiling data: " << (100.f * knowledge_count / knowledge_max) << "%" << endl;
			cout << "     fully explored: " << explored_count << "/" << scenario.size() << " settings" << endl;
			cout << "     mean workload:  " << (100.f * load_total / (capacity*frames)) << "%" << endl;
			cout << "     pess.workload:  " << (100.f * load_pess / (capacity*frames)) << "%" << endl;
			cout << "     mean burden:    #" << (load_total / frames)
				<< " / high: #" << (high_load_total / frames)
				<< " / light: #" << (light_load_total / frames) << endl;
			cout << "     mean value:     $" << (value_total / frames)
				<< " / high: $" << knapsack.stats.highest.net_value
				<< " / light: $" << knapsack.stats.lightest.net_value << endl;

		}
		

		while (true)
		{
			cout << "What now?\n"
				"  R = go again (default action)\n"
				"  Q = quit\n"
				">> ";
			std::string s;
			std::getline(std::cin, s);

			if (s.length() == 0 || s[0] == 'r' || s[0] == 'R' || s[0] == '\r' || s[0] == '\n')
			{
				break;
			}
			/*else if (s[0] == 'v' || s[0] == 'V')
			{
				describe_problem(cout, goblin.knapsack);
			}*/
			else if (s[0] == 'q' || s[0] == 'Q')
			{
				return;
			}
			/*else if (s[0] == 's' || s[0] == 'S')
			{
				std::ofstream out("solution.svg");
				write_svg(out, problem, max_burden);
				if (out.good()) cout << "saved to solution.svg" << endl;
				else            cout << "couldn't save to solution.svg" << endl;
			}*/
			else
			{
				cout << "unknown command." << endl;
			}
		}
	}
}

int main(int argc, char **argv)
{
	test_goblin();
	test_knapsack();
}