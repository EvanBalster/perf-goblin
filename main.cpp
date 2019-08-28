#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <chrono>

#include "perf_goblin.h"


using namespace perf_goblin;

using std::cout;
using std::endl;


std::ostream& operator<<(std::ostream &out, const Problem::option_t &option)
{
	return out << "(#" << option.burden << " $" << option.value << ')';
}

std::ostream& operator<<(std::ostream &out, const Problem::options_t &options)
{
	out << '{';
	for (size_t i = 0; i < options.size(); ++i)
	{
		out << (i ? ", " : "") << options[i];
	}
	out << '}';
	return out;
}

std::ostream& operator<<(std::ostream &out, const Problem::statistics_t &stats)
{
	return out << "(#" << stats.net_burden << " $" << stats.net_value << " @" << stats.net_points << ")";
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

void write_svg(std::ostream &out, Problem &problem, Problem::statistics_t solution_stats, float max_burden)
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
		auto &d = problem.decisions[i];
		auto &c = d.options[d.choice];
		if (c.burden <= 0.f) continue;

		box_t box = {c.burden, c.value / c.burden, i};
		boxes.push_back(box);
	}
	std::sort(boxes.begin(), boxes.end(), [](const box_t &l, const box_t &r) -> bool {return l.value_burden_ratio > r.value_burden_ratio;});

	for (auto &box : boxes)
	{
		auto &d = problem.decisions[box.index];
		auto &c = d.options[d.choice];

		float y = 150.f;
		float wid = x_scale * box.burden, hei = y_scale * box.value_burden_ratio;

		if (c.value == 0.f) {y += 10.f; hei = 10.f;}

		out << "\t\t<rect x=\"" << x << "\" y=\"" << (y-hei)
			<< "\" width=\"" << wid << "\" height=\"" << hei;
		if (c.value == 0.f) out << "\" fill=\"#CC6666";
		else if (d.options.size() == 2) out << "\" fill=\"#55BBBB";
		out << "\"/>";
		out << "<!-- " << c << " -->" "\n";
		x += wid;
	}

	out << "\t</g>\n";

	out << "</svg>";
}

void describe_problem(std::ostream &out, Problem &problem)
{
	out << "problem & solution:" << endl;
	for (unsigned i = 0; i < problem.decisions.size(); ++i)
	{
		auto &decision = problem.decisions[i];
		out << " " << std::setw(3) << (i+1) << ": ";
		auto opts = decision.options.size();
		switch (opts)
		{
		case 0: case 1: out << "   ";                       break;
		case 2:  out << (decision.choice ? " on" : "off");  break;
		default: out << (decision.choice+1) << "/" << opts; break;
		}
		
		//<< " " << decision.options[decision.choice]
		//<< " @" << std::setw(2) << decision.options[decision.choice]._points << endl
		out << " ~ " << decision.options << endl;
	}
	out << endl;

	/*out << "solution:" << endl;
	for (auto &decision : problem.decisions)
	{
		cout << "  d" << std::setw(3) << (&decision-&problem.decisions[0] + 1)
			
	}*/
}

int main(int argc, char **argv)
{
	Problem problem;

	srand(unsigned(time(NULL)));

	cout << std::fixed << std::setprecision(1);

	while (true)
	{
		problem.decisions.clear();

		cout << "Generating a new multiple-choice knapsack problem." << endl;
		cout << endl;

		cout << "  problem:" << endl;

		for (unsigned i = 0; i < 50; ++i)
		{
			switch (rand() & 7)
			{
			case 0:
				// Fixed burden
				problem.add_burden(random_burden());
				break;

			case 1:
				// Fixed incentive
				problem.add_incentive(random_value(random_burden()) - random_value(random_burden()));
				break;
			
			case 2: case 3: case 4:
				// Binary choice
				{
					float burden = random_burden();
					problem.add_binary_item(burden, random_value(burden));
				}
				break;

			case 5: case 6:
				// Multiple choice, orderly
				{
					problem.decisions.emplace_back(Problem::decision_t());
					auto &decision = problem.decisions.back();
					float burden = 0.f, value = 0.f;
					unsigned count = 2u + (1u + (rand() & 3u)) * (1u + (rand() & 7u));
					for (unsigned i = 0; i < count; ++i)
					{
						float new_burden = random_burden() * (2.f/count);
						burden += new_burden;
						value += random_value(new_burden);
						Problem::option_t option = {burden, value};
						decision.options.push_back(option);
					}
				}
				break;

			case 7: default:
				// Multiple choice, chaotic
				{
					problem.decisions.emplace_back(Problem::decision_t());
					auto &decision = problem.decisions.back();
					unsigned count = 2u + (1u + (rand() & 3u)) * (1u + (rand() & 7u));
					for (unsigned i = 0; i < count; ++i)
					{
						float burden = random_burden() * (2.f/count);
						float value = random_value(burden);
						Problem::option_t option = {burden, value};
						decision.options.push_back(option);
					}
				}
				break;
			}
		}

		unsigned precision = 50;
		float max_burden = 0.f;
		{
			size_t N = problem.decisions.size();
			for (size_t i = N/2; i < N; ++i)
				max_burden += random_burden();
		}

		{
			size_t total_options = 0;
			for (auto &d : problem.decisions) total_options += d.options.size();

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
		auto stats_solution = problem.decide(max_burden, precision);
		float time_taken = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - prof_time).count();
		cout << endl;

		{
			size_t table_size = problem.optimums.store.size(), table_empties = 0;

			for (auto &item : problem.optimums.store)
			{
				if (!(item.net_burden < std::numeric_limits<float>::infinity())) ++table_empties;
			}

			cout << "  solver data:" << endl;
			cout << "    solver time: " << (1000000.f*time_taken) << " us" << endl;
			cout << "    solution is: ";
			if      (problem._iterations)       cout << "approximate optimum (" << problem._iterations << " iterations)";
			else if (stats_solution.net_points) cout << "ideal";
			else                                cout << "impossible (selecting minimum burden)";
			cout << endl;
			if (table_size)
			{
				cout << "    table size:  " << table_size << endl;
				cout << "    table fill:  " << (100.f - 100.f*table_empties / table_size) << "%" << endl;
			}
			cout << endl;
		}

		{
			cout << "  solution stats:" << endl;
			cout << "    min-burden: " << problem._stats_lightest << endl;
			cout << "    max-score:  " << problem._stats_highest << endl;
			cout << "    chosen:     " << stats_solution << endl;
			cout << "    efficiency: "
				<< "(#" << (100.f*stats_solution.net_burden / problem._stats_highest.net_burden) << "%"
				<< " $" << (100.f*stats_solution.net_value  / problem._stats_highest.net_value ) << "%"
				<< " @" << (100.f*stats_solution.net_points / problem._stats_highest.net_points) << "%) compared to max-score" << endl;
			cout << endl;
		}

		

		while (true)
		{
			cout << "What now?\n"
				"  R = go again\n"
				"  V = view problem and solution\n"
				"  S = save SVG diagram\n"
				"  Q = quit\n"
				">> ";
			std::string s;
			std::cin >> s;

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
				return 0;
			}
			else if (s[0] == 's' || s[0] == 'S')
			{
				std::ofstream out("solution.svg");
				write_svg(out, problem, stats_solution, max_burden);
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