#include "SIPP.h"
#include "SpaceTimeAStar.h"

void SIPP::updatePath(const LLNode* goal, vector<PathEntry> &path)
{
	path.resize(goal->timestep + 1);
	// num_of_conflicts = goal->num_of_conflicts;

	const auto* curr = goal;
	while (curr->parent != nullptr) // non-root node
	{
		const auto* prev = curr->parent;
		int t = prev->timestep + 1;
		while (t < curr->timestep)
		{
			path[t].location = prev->location; // wait at prev location
			t++;
		}
		path[curr->timestep].location = curr->location; // move to curr location
		curr = prev;
	}
	assert(curr->timestep == 0);
	path[0].location = curr->location;
}


// find path by A*
// Returns a path that minimizes the collisions with the paths in the path table, breaking ties by the length
Path SIPP::findPath(const ConstraintTable& constraint_table)
{
    ReservationTable reservation_table(constraint_table, goal_location);

    Path path;
    num_expanded = 0;
    num_generated = 0;
    Interval interval = reservation_table.get_first_safe_interval(start_location);
    if (get<0>(interval) > 0)
        return path;
    auto holding_time = constraint_table.getHoldingTime(goal_location, constraint_table.length_min);
    // generate start and add it to the OPEN & FOCAL list
    
    auto start = new SIPPNode(start_location, 0, max(my_heuristic[start_location], holding_time), nullptr, 0, interval, 0);
    num_generated++;
    start->in_openlist = true;
    start->focal_handle = focal_list.push(start); // we only use focal list; no open list is used
    allNodes_table.insert(start);
    while (!focal_list.empty())
    {
        auto* curr = focal_list.top();
        focal_list.pop();
        curr->in_openlist = false;
        num_expanded++;
        assert(curr->location >= 0);
        // check if the popped node is a goal
        if (curr->is_goal)
        {
            updatePath(curr->parent, path);
            break;
        }
        else if (curr->location == goal_location && // arrive at the goal location
                 !curr->wait_at_goal && // not wait at the goal location
                 curr->timestep >= holding_time) // the agent can hold the goal location afterward
        {
            int future_collisions = constraint_table.getFutureNumOfCollisions(curr->location, curr->timestep);
            if (future_collisions == 0)
            {
                updatePath(curr, path);
                break;
            }
            // generate a goal node
            auto goal = new SIPPNode(*curr);
            goal->is_goal = true;
            goal->parent = curr;
            goal->num_of_conflicts += future_collisions;
            // try to retrieve it from the hash table
            auto it = allNodes_table.find(goal);
            if (it == allNodes_table.end())
            {
                goal->focal_handle = focal_list.push(goal);
                goal->in_openlist = true;
                num_generated++;
                allNodes_table.insert(goal);
            }
            else // update existing node's if needed (only in the open_list)
            {
                auto existing_next = *it;
                if (existing_next->timestep > goal->timestep || // prefer the one with smaller timestep
                    (existing_next->timestep == goal->timestep &&
                     existing_next->num_of_conflicts > goal->num_of_conflicts)) // or it remains the same but there's fewer conflicts
                {
                    assert(existing_next->in_openlist);
                    existing_next->copy(*goal);	// update existing node
                    focal_list.update(existing_next->focal_handle);
                }
                delete (goal);
            }
        }
        for (int next_location : instance.getNeighbors(curr->location)) // move to neighboring locations
        {
            int next_h_val = my_heuristic[next_location];
            for (auto& interval : reservation_table.get_safe_intervals(
                    curr->location, next_location, curr->timestep + 1, get<1>(curr->interval) + 1))
            {
                int next_timestep = max(curr->timestep + 1, (int)get<0>(interval));
                if (next_timestep + next_h_val > constraint_table.length_max)
                    break;
                generateChildToFocal(interval, curr, next_location, next_h_val);
            }
        }  // end for loop that generates successors
        // wait at the current location
        bool found = reservation_table.find_safe_interval(interval, curr->location, get<1>(curr->interval));
        if (found)
        {
            generateChildToFocal(interval, curr, curr->location, curr->h_val);
        }
    }  // end while loop

    //if (path.empty())
    //    printSearchTree();
    releaseNodes();
    return path;
}

Path SIPP::findOptimalPath(const HLNode& node, const ConstraintTable& initial_constraints,
	const vector<Path*>& paths, int agent, int lowerbound)
{
	return findSuboptimalPath(node, initial_constraints, paths, agent, lowerbound, 1).first;
}

// find path by SIPP
// Returns a shortest path that satisfies the constraints of the give node  while
// minimizing the number of internal conflicts (that is conflicts with known_paths for other agents found so far).
// lowerbound is an underestimation of the length of the path in order to speed up the search.
pair<Path, int> SIPP::findSuboptimalPath(const HLNode& node, const ConstraintTable& initial_constraints,
	const vector<Path*>& paths, int agent, int lowerbound, double w)
{
	this->w = w;

	// build constraint table
    auto t = clock();
    ConstraintTable constraint_table(initial_constraints);
    constraint_table.insert2CT(node, agent);
	runtime_build_CT = (double)(clock() - t) / CLOCKS_PER_SEC;
	int holding_time = constraint_table.getHoldingTime(goal_location, constraint_table.length_min);
	t = clock();
    constraint_table.insert2CAT(agent, paths);
	runtime_build_CAT = (double)(clock() - t) / CLOCKS_PER_SEC;

	// build reservation table
	ReservationTable reservation_table(constraint_table, goal_location);

    Path path;
	num_expanded = 0;
	num_generated = 0;
	Interval interval = reservation_table.get_first_safe_interval(start_location);
	if (get<0>(interval) > 0)
		return {path, 0};

	 // generate start and add it to the OPEN list
	auto start = new SIPPNode(start_location, 0, max(my_heuristic[start_location], holding_time), nullptr, 0, interval, 0);

	num_generated++;
	start->open_handle = open_list.push(start);
	start->focal_handle = focal_list.push(start);
	start->in_openlist = true;
	allNodes_table.insert(start);
	min_f_val = max(holding_time, max((int)start->getFVal(), lowerbound));


	while (!open_list.empty()) 
	{
		updateFocalList(); // update FOCAL if min f-val increased
		SIPPNode* curr = focal_list.top(); focal_list.pop();
		open_list.erase(curr->open_handle);
		curr->in_openlist = false;
		num_expanded++;

		// check if the popped node is a goal node
        if (curr->location == goal_location && // arrive at the goal location
			!curr->wait_at_goal && // not wait at the goal location
			curr->timestep >= holding_time) // the agent can hold the goal location afterward
        {
            updatePath(curr, path);
            break;
        }

        for (int next_location : instance.getNeighbors(curr->location)) // move to neighboring locations
		{
			for (auto interval : reservation_table.get_safe_intervals(
				curr->location, next_location, curr->timestep + 1, get<1>(curr->interval) + 1))
			{
				generateChild(interval, curr, next_location, reservation_table);
			}
		}  // end for loop that generates successors
		   
		// wait at the current location
		bool found = reservation_table.find_safe_interval(interval, curr->location, get<1>(curr->interval));
		if (found)
		{
			generateChild(interval, curr, curr->location, reservation_table);
		}
	}  // end while loop
	  
	  // no path found
	releaseNodes();
	return {path, min_f_val};
}

void SIPP::updateFocalList()
{
	auto open_head = open_list.top();
	if (open_head->getFVal() > min_f_val)
	{
		int new_min_f_val = (int)open_head->getFVal();
		for (auto n : open_list)
		{
			if (n->getFVal() > w * min_f_val && n->getFVal() <= w * new_min_f_val)
				n->focal_handle = focal_list.push(n);
		}
		min_f_val = new_min_f_val;
	}
}


inline SIPPNode* SIPP::popNode()
{
	auto node = focal_list.top(); focal_list.pop();
	open_list.erase(node->open_handle);
	node->in_openlist = false;
	num_expanded++;
	return node;
}


inline void SIPP::pushNode(SIPPNode* node)
{
	node->open_handle = open_list.push(node);
	node->in_openlist = true;
	num_generated++;
	if (node->getFVal() <= w * min_f_val)
		node->focal_handle = focal_list.push(node);
}


void SIPP::releaseNodes()
{
	open_list.clear();
	focal_list.clear();
	for (auto node: allNodes_table)
		delete node;
	allNodes_table.clear();
}

void SIPP::generateChild(const Interval& interval, SIPPNode* curr, int next_location,
                         const ReservationTable& reservation_table)
{
    // compute cost to next_id via curr node
    int next_timestep = max(curr->timestep + 1, (int)get<0>(interval));
    int next_g_val = next_timestep;
    int next_h_val = max(my_heuristic[next_location], curr->getFVal() - next_g_val);  // path max
    if (next_g_val + next_h_val > reservation_table.constraint_table.length_max)
        return;
    int next_conflicts = curr->num_of_conflicts + (int)get<2>(interval) * (next_timestep - curr->timestep);

    // generate (maybe temporary) node
    auto next = new SIPPNode(next_location, next_g_val, next_h_val, curr, next_timestep, interval, next_conflicts);
    if (next_location == goal_location && curr->location == goal_location)
        next->wait_at_goal = true;
    // try to retrieve it from the hash table
    auto it = allNodes_table.find(next);
    if (it == allNodes_table.end())
    {
        pushNode(next);
        allNodes_table.insert(next);
        return;
    }
    // update existing node's if needed (only in the open_list)

    auto existing_next = *it;
    if (existing_next->timestep > next->timestep || // prefer the one with smaller timestep
        (existing_next->timestep == next->timestep &&
         existing_next->num_of_conflicts > next->num_of_conflicts)) // or it remains the same but there's fewer conflicts
    {
        if (!existing_next->in_openlist) // if its in the closed list (reopen)
        {
            existing_next->copy(*next);
            pushNode(existing_next);
        }
        else
        {
            bool add_to_focal = false;  // check if it was above the focal bound before and now below (thus need to be inserted)
            bool update_in_focal = false;  // check if it was inside the focal and needs to be updated (because f-val changed)
            bool update_open = false;
            if ((next_g_val + next_h_val) <= w * min_f_val)
            {  // if the new f-val qualify to be in FOCAL
                if (existing_next->getFVal() > w * min_f_val)
                    add_to_focal = true;  // and the previous f-val did not qualify to be in FOCAL then add
                else
                    update_in_focal = true;  // and the previous f-val did qualify to be in FOCAL then update
            }
            if (existing_next->getFVal() > next_g_val + next_h_val)
                update_open = true;

            existing_next->copy(*next);	// update existing node

            if (update_open)
                open_list.increase(existing_next->open_handle);  // increase because f-val improved
            if (add_to_focal)
                existing_next->focal_handle = focal_list.push(existing_next);
            if (update_in_focal)
                focal_list.update(existing_next->focal_handle);  // should we do update? yes, because number of conflicts may go up or down
        }
    }

    delete(next);  // not needed anymore -- we already generated it before
}
void SIPP::generateChildToFocal(const Interval& interval, SIPPNode* curr, int next_location, int next_h_val)
{
    int next_timestep = max(curr->timestep + 1, (int)get<0>(interval));
    next_h_val = max(next_h_val, curr->getFVal() - next_timestep); // path max
    // generate (maybe temporary) node
    auto next = new SIPPNode(next_location, next_timestep, next_h_val, curr, next_timestep, interval,
            curr->num_of_conflicts + (int)get<2>(interval));
    if (next_location == goal_location && curr->location == goal_location)
        next->wait_at_goal = true;
    // try to retrieve it from the hash table
    auto it = allNodes_table.find(next);
    if (it == allNodes_table.end())
    {
        next->focal_handle = focal_list.push(next);
        next->in_openlist = true;
        num_generated++;
        allNodes_table.insert(next);
        return;
    }
    // update existing node's if needed (only in the open_list)

    auto existing_next = *it;
    //if (existing_next->num_of_conflicts > next->num_of_conflicts ||
    //    (existing_next->num_of_conflicts == next->num_of_conflicts &&
    //     existing_next->getFVal() > next->getFVal()))
    if (existing_next->timestep > next->timestep || // prefer the one with smaller timestep
        (existing_next->timestep == next->timestep &&
         existing_next->num_of_conflicts > next->num_of_conflicts)) // or it remains the same but there's fewer conflicts
    {
        existing_next->copy(*next);
        if (!existing_next->in_openlist) // if its in the closed list (reopen)
        {
            existing_next->focal_handle = focal_list.push(existing_next);
            existing_next->in_openlist = true;
        }
        else
        {
            focal_list.update(existing_next->focal_handle);  // should we do update? yes, because number of conflicts may go up or down
        }
    }

    delete(next);  // not needed anymore -- we already generated it before
}

// TODO:: currently this is implemented in A*, not SIPP
int SIPP::getTravelTime(int start, int end, const ConstraintTable& constraint_table, int upper_bound)
{
	int length = MAX_TIMESTEP;
	auto root = new SIPPNode(start, 0, compute_heuristic(start, end), nullptr, 0, Interval(0, 1, 0), 0);
	root->open_handle = open_list.push(root);  // add root to heap
	allNodes_table.insert(root);       // add root to hash_table (nodes)
	SIPPNode* curr = nullptr;
	auto static_timestep = constraint_table.getMaxTimestep(); // everything is static after this timestep
	while (!open_list.empty())
	{
		curr = open_list.top(); open_list.pop();
		if (curr->location == end)
		{
			length = curr->g_val;
			break;
		}
		list<int> next_locations = instance.getNeighbors(curr->location);
		next_locations.emplace_back(curr->location);
		for (int next_location : next_locations)
		{
			int next_timestep = curr->timestep + 1;
			int next_g_val = curr->g_val + 1;
			if (static_timestep <= curr->timestep)
			{
				if (curr->location == next_location)
				{
					continue;
				}
				next_timestep--;
			}
			if (!constraint_table.constrained(next_location, next_timestep) &&
				!constraint_table.constrained(curr->location, next_location, next_timestep))
			{  // if that grid is not blocked
				int next_h_val = compute_heuristic(next_location, end);
				if (next_g_val + next_h_val >= upper_bound) // the cost of the path is larger than the upper bound
					continue;
				auto next = new SIPPNode(next_location, next_g_val, next_h_val, nullptr, next_timestep,
				        Interval(next_timestep, next_timestep + 1, 0), 0);
				auto it = allNodes_table.find(next);
				if (it == allNodes_table.end())
				{  // add the newly generated node to heap and hash table
					next->open_handle = open_list.push(next);
					allNodes_table.insert(next);
				}
				else {  // update existing node's g_val if needed (only in the heap)
					delete(next);  // not needed anymore -- we already generated it before
					auto existing_next = *it;
					if (existing_next->g_val > next_g_val)
					{
						existing_next->g_val = next_g_val;
						existing_next->timestep = next_timestep;
						open_list.increase(existing_next->open_handle);
					}
				}
			}
		}
	}
	releaseNodes();
	return length;
	/*int length = INT_MAX;
	// generate a heap that can save nodes (and a open_handle)
	pairing_heap< SIPPNode*, compare<SIPPNode::compare_node> > open_list;
	// boost::heap::pairing_heap< AStarNode*, boost::heap::compare<LLNode::compare_node> >::handle_type open_handle;
	unordered_set<SIPPNode*, SIPPNode::NodeHasher, SIPPNode::eqnode> nodes;

	Interval interval = reservation_table.get_first_safe_interval(start);
	assert(get<0>(interval) == 0);
	auto root = new SIPPNode(start, 0, instance.getManhattanDistance(start, end), nullptr, 0, interval);
	root->open_handle = open_list.push(root);  // add root to heap
	nodes.insert(root);       // add root to hash_table (nodes)

	while (!open_list.empty())
	{
		auto curr = open_list.top(); open_list.pop();
		if (curr->location == end)
		{
			length = curr->g_val;
			break;
		}
		for (int next_location : instance.getNeighbors(curr->location))
		{
			if ((curr->location == blocked.first && next_location == blocked.second) ||
				(curr->location == blocked.second && next_location == blocked.first)) // use the prohibited edge
			{
				continue;
			}

			for (auto interval : reservation_table.get_safe_intervals(
				curr->location, next_location, curr->timestep + 1, get<1>(curr->interval) + 1))
			{
				int next_timestep = max(curr->timestep + 1, (int)get<0>(interval));
				int next_g_val = next_timestep;
				int next_h_val = instance.getManhattanDistance(next_location, end);
				if (next_g_val + next_h_val >= upper_bound) // the cost of the path is larger than the upper bound
					continue;
				auto next = new SIPPNode(next_location, next_g_val, next_h_val, nullptr, next_timestep, interval);
				auto it = nodes.find(next);
				if (it == nodes.end())
				{  // add the newly generated node to heap and hash table
					next->open_handle = open_list.push(next);
					nodes.insert(next);
				}
				else {  // update existing node's g_val if needed (only in the heap)
					delete(next);  // not needed anymore -- we already generated it before
					auto existing_next = *it;
					if (existing_next->g_val > next_g_val)
					{
						existing_next->g_val = next_g_val;
						existing_next->timestep = next_timestep;
						open_list.update(existing_next->open_handle);
					}
				}
			}
		}
	}
	open_list.clear();
	for (auto node : nodes)
	{
		delete node;
	}
	nodes.clear();
	return length;*/
}


void SIPP::printSearchTree() const
{
    vector<list<SIPPNode*>> nodes;
    for (const auto& n : allNodes_table)
    {
        if (nodes.size() <= n->timestep)
            nodes.resize(n->timestep + 1);
        nodes[n->timestep].emplace_back(n);
    }
    cout << "Search Tree" << endl;
    for(int t = 0; t < nodes.size(); t++)
    {
        cout << "t=" << t << ":\t";
        for (const auto & n : nodes[t])
            cout << *n << "[" << get<0>(n->interval) << "," << get<1>(n->interval) << "],\t";
        cout << endl;
    }
}