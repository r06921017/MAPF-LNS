#pragma once
#include "LNS.h"

enum init_destroy_heuristic { TARGET_BASED, COLLISION_BASED, INIT_COUNT };

class InitLNS
{
public:
    vector<Agent>& agents;
    list<IterationStats> iteration_stats; //stats about each iteration
    double initial_solution_runtime = 0;
    double runtime = 0;
    int initial_sum_of_costs = -1;
    int sum_of_costs = -1;
    int sum_of_costs_lowerbound = -1;
    int sum_of_distances = -1;
    int num_of_colliding_pairs = -1;
    double average_group_size = -1;
    size_t num_LL_generated = 0;
    int num_of_failures = 0; // #replanning that fails to find any solutions
    InitLNS(const Instance& instance, vector<Agent>& agents, double time_limit, string init_algo_name,
            string replan_algo_name,string init_destory_name, int neighbor_size, int screen);

    bool getInitialSolution();
    bool run();
    void validateSolution() const;
    void writeIterStatsToFile(string file_name) const;
    void writeResultToFile(string file_name) const;
    void writePathsToFile(string file_name) const;
    string getSolverName() const { return "InitLNS(" + init_algo_name + ";" + replan_algo_name + ")"; }

    void printPath() const;
private:
    int num_neighbor_sizes = 1; //4; // so the neighbor size could be 2, 4, 8, 16

    // input params
    const Instance& instance; // avoid making copies of this variable as much as possible
    double time_limit;
    double replan_time_limit; // time limit for replanning
    string init_algo_name;
    string replan_algo_name;
    bool init_lns; // use LNS to find initial solutions
    int screen;
    init_destroy_heuristic init_destroy_strategy = COLLISION_BASED;
    int neighbor_size;


    high_resolution_clock::time_point start_time;


    PathTableWC path_table; // 1. stores the paths of all agents in a time-space table;
    // 2. avoid making copies of this variable as much as possible.

    vector<set<int>> collision_graph;
    Neighbor neighbor;
    vector<int> goal_table;


    unordered_set<int> tabu_list; // used by randomwalk strategy
    list<int> intersections;

    // adaptive LNS
    bool ALNS = false;
    double decay_factor = 0.01;
    double reaction_factor = 0.01;
    vector<double> destroy_weights;
    int selected_neighbor;

    bool runPP();
    bool runGCBS();
    bool runPBS();

    void updateCollidingPairs(set<pair<int, int>>& colliding_pairs, int agent_id, const Path& path) const;

    void chooseDestroyHeuristicbyALNS();
    //bool generateNeighborByStart();

    bool generateNeighborByCollisionGraph();

    bool generateNeighborByTarget();


    int findRandomAgent() const;
    int randomWalk(int agent_id);

    void printCollisionGraph() const;

    static unordered_map<int, set<int>>& findConnectedComponent(const vector<set<int>>& graph, int vertex,
            unordered_map<int, set<int>>& sub_graph);
};
