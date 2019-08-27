#include <iostream>
#include <iomanip>
#include <cmath>
#include <ctime>
#include <cstdlib>

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
	return ((rv & 255u) / 255.f) * (((rv >> 8u) & 255u) / 2.55f);
}

static float random_value()
{
	unsigned rv = rand();
	return ((rv & 255u) / 255.f) * (((rv >> 8u) & 255u) / 2.55f);
}

int main(int argc, char **argv)
{
	Problem problem;

	srand(unsigned(time(NULL)));

	cout << std::fixed << std::setprecision(2);

	bool verbose = false;

	while (true)
	{
		problem.decisions.clear();

		cout << "Generating a new multiple-choice knapsack problem." << endl;
		cout << endl;

		cout << "  problem:" << endl;

		for (unsigned i = 0; i < 100; ++i)
		{
			switch (rand() & 7)
			{
			case 0:
				// No choice
				problem.add_burden(random_burden());
				break;

			case 1: case 2: case 3:
				// Binary choice
				problem.add_binary_item(random_burden(), random_value());
				break;

			case 4: case 5: case 6:
				// Multiple choice, orderly
				{
					problem.decisions.emplace_back(Problem::decision_t());
					auto &decision = problem.decisions.back();
					float burden = 0.f, value = 0.f;
					for (unsigned i = 0; i < 2u + (rand() & 15u); ++i)
					{
						burden += random_burden();
						value += random_value();
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
					for (unsigned i = 0; i < 2u + (rand() & 15u); ++i)
					{
						float burden = random_burden();
						float value = random_value();
						Problem::option_t option = {burden, value};
						decision.options.push_back(option);
					}
				}
				break;
			}
			if (verbose) cout << "    " << i << ": " << problem.decisions.back().options << endl;
		}

		unsigned precision = 50;
		float max_burden = 0.f;
		{
			size_t N = problem.decisions.size();
			for (size_t i = 0; i < N; ++i)
				max_burden += random_burden();
		}

		{
			size_t total_options = 0;
			for (auto &d : problem.decisions) total_options += d.options.size();

			cout << "    decisions: " << problem.decisions.size() << endl;
			cout << "    total options: " << total_options << endl;
			cout << "    mean options per decision: " << (float(total_options)/problem.decisions.size()) << endl;

			cout << "    burden limit: #" << max_burden << endl;
			cout << "    precision: " << precision << endl;
			cout << "    iteration bound (est): " << (precision * problem.decisions.size() * total_options) << endl;
			cout << endl;
		}

		// Run solver
		cout << "    (...solving...)" << endl;
		auto stats_solution = problem.decide(max_burden, precision);
		cout << endl;

		{
			size_t table_size = problem.optimums.store.size(), table_empties = 0;

			for (auto &item : problem.optimums.store)
			{
				if (!(item.net_burden < std::numeric_limits<float>::infinity())) ++table_empties;
			}

			cout << "  solver data:" << endl;
			cout << "    iterations: " << problem._iterations << endl;
			cout << "    table size: " << table_size << endl;
			cout << "    table fill: " << (100.f - 100.f*table_empties / table_size) << "%" << endl;
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
				<< " @" << (100.f*stats_solution.net_points / problem._stats_highest.net_points) << "%)" << endl;
			cout << endl;
		}
		

		if (verbose)
		{
			cout << "  solution:" << endl;
			for (auto &decision : problem.decisions)
			{
				cout << "    " << (&decision-&problem.decisions[0])
					<< " -> " << decision.choice << "/" << decision.options.size()
					<< ": " << decision.options[decision.choice]
					<< " @" << decision.options[decision.choice]._points << endl;
			}
			cout << endl;
		}

		// pause...
		cout << "press ENTER to go again." << endl;
		std::cin.ignore(255, '\n');
	}
}