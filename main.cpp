#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <chrono>

#include "knapsack.h"
#include "goblin.h"


using namespace perf_goblin;

using std::cout;
using std::endl;


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
	unsigned rv = rand();
	return .2f + .8f * + ((rv & 255u) / 255.f) * (((rv >> 8u) & 255u) / 2.55f);
}

static float random_value(float burden)
{
	unsigned rv = rand();
	float uncorrelated_val = ((rv & 255u) / 255.f) * (((rv >> 8u) & 255u) / 2.55f);
	return std::sqrt(burden * uncorrelated_val);
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
	switch (rand() & 7)
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
			unsigned count = 2u + (1u + (rand() & 3u)) * (1u + (rand() & 7u));
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
			unsigned count = 2u + (1u + (rand() & 3u)) * (1u + (rand() & 7u));
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
		float max_burden = 0.f;
		{
			size_t N = problem.decisions.size();
			for (size_t i = N/2; i < N; ++i)
				max_burden += random_burden();
		}

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
	std::vector<Option_t> options;

	SimSetting() :
		Setting(nullptr, 0)
	{
		generate_decision(_decision, options);
	}
};

void test_goblin()
{
	while (true)
	{
		Goblin goblin;

		//std::list<SimSetting> scenario;

		cout << "Generating a new performance control scenario." << endl;

		/*for (size_t i = 0; i < 50; ++i)
		{
			scenario.emplace_back();
			goblin.add(&scenario.back());
		}*/

		return;
	}
}

int main(int argc, char **argv)
{
	test_knapsack();
}