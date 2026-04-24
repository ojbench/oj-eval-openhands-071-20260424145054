#pragma once
// #include "interface.h"
// #include "definition.h"
// You should not use those functions in runtime.h

#include <vector>
#include <algorithm>
#include <random>
#include <cmath>
#include <queue>
#include <set>

namespace oj {

auto generate_tasks(const Description &desc) -> std::vector <Task> {
    std::vector<Task> tasks;
    tasks.reserve(desc.task_count);
    
    std::mt19937 rng(42);
    
    // Choose target sums within valid range
    priority_t min_priority_sum = std::max(desc.priority_sum.min, desc.task_count * desc.priority_single.min);
    priority_t max_priority_sum = std::min(desc.priority_sum.max, desc.task_count * desc.priority_single.max);
    
    if (min_priority_sum > max_priority_sum) {
        min_priority_sum = max_priority_sum = desc.task_count * desc.priority_single.min;
    }
    
    priority_t target_priority_sum = (min_priority_sum + max_priority_sum) / 2;
    
    time_t min_exec_sum = std::max(desc.execution_time_sum.min, desc.task_count * desc.execution_time_single.min);
    time_t max_exec_sum = std::min(desc.execution_time_sum.max, desc.task_count * desc.execution_time_single.max);
    
    if (min_exec_sum > max_exec_sum) {
        min_exec_sum = max_exec_sum = desc.task_count * desc.execution_time_single.min;
    }
    
    time_t target_exec_sum = (min_exec_sum + max_exec_sum) / 2;
    
    time_t current_exec_sum = 0;
    priority_t current_priority_sum = 0;
    
    for (task_id_t i = 0; i < desc.task_count; ++i) {
        Task task;
        task.launch_time = 0;
        
        task_id_t remaining_tasks = desc.task_count - i;
        
        // Execution time
        if (i == desc.task_count - 1) {
            task.execution_time = target_exec_sum - current_exec_sum;
            task.execution_time = std::max(task.execution_time, desc.execution_time_single.min);
            task.execution_time = std::min(task.execution_time, desc.execution_time_single.max);
        } else {
            time_t remaining_exec = target_exec_sum - current_exec_sum;
            time_t remaining_budget = remaining_exec - (remaining_tasks - 1) * desc.execution_time_single.min;
            time_t avg_exec = remaining_exec / remaining_tasks;
            time_t exec_min = desc.execution_time_single.min;
            time_t exec_max = std::min(desc.execution_time_single.max, remaining_budget);
            exec_max = std::max(exec_max, exec_min);
            task.execution_time = std::uniform_int_distribution<time_t>(exec_min, exec_max)(rng);
        }
        current_exec_sum += task.execution_time;
        
        // Priority
        if (i == desc.task_count - 1) {
            task.priority = target_priority_sum - current_priority_sum;
            task.priority = std::max(task.priority, desc.priority_single.min);
            task.priority = std::min(task.priority, desc.priority_single.max);
        } else {
            priority_t remaining_priority = target_priority_sum - current_priority_sum;
            priority_t remaining_budget = remaining_priority - (remaining_tasks - 1) * desc.priority_single.min;
            priority_t priority_min = desc.priority_single.min;
            priority_t priority_max = std::min(desc.priority_single.max, remaining_budget);
            priority_max = std::max(priority_max, priority_min);
            task.priority = std::uniform_int_distribution<priority_t>(priority_min, priority_max)(rng);
        }
        current_priority_sum += task.priority;
        
        // Deadline
        time_t min_time_needed = task.execution_time + PublicInformation::kStartUp + PublicInformation::kSaving;
        time_t deadline_min = std::max(desc.deadline_time.min, min_time_needed);
        time_t deadline_max = desc.deadline_time.max;
        if (deadline_min <= deadline_max) {
            task.deadline = std::uniform_int_distribution<time_t>(deadline_min, deadline_max)(rng);
        } else {
            task.deadline = deadline_max;
        }
        
        tasks.push_back(task);
    }
    
    return tasks;
}

} // namespace oj

namespace oj {

struct TaskInfo {
    time_t execution_time;
    time_t deadline;
    priority_t priority;
    double progress;
    int state; // 0: free, 1: launching, 2: saving
    time_t state_end_time;
    cpu_id_t current_cpus;
};

static std::vector<TaskInfo> all_tasks;
static cpu_id_t available_cpus = PublicInformation::kCPUCount;

auto schedule_tasks(time_t time, std::vector <Task> list, const Description &desc) -> std::vector<Policy> {
    static task_id_t task_id = 0;
    const task_id_t first_id = task_id;
    const task_id_t last_id = task_id + list.size();
    task_id += list.size();

    // Add new tasks
    for (const auto& task : list) {
        all_tasks.push_back({
            task.execution_time,
            task.deadline,
            task.priority,
            0.0,
            0,
            0,
            0
        });
    }
    
    std::vector<Policy> policies;
    
    // First, handle saving tasks that need to be saved
    for (task_id_t id = 0; id < all_tasks.size(); ++id) {
        auto& info = all_tasks[id];
        
        // Save tasks that are in launch state and should be saved
        if (info.state == 1) {
            time_t time_in_launch = time - (info.state_end_time - PublicInformation::kStartUp);
            if (time_in_launch >= PublicInformation::kStartUp) {
                time_t remaining_time = info.deadline - time;
                
                // Calculate work done
                double work_done = time_policy(time_in_launch, info.current_cpus);
                
                // Save if we're close to deadline or task is complete
                if (remaining_time <= PublicInformation::kSaving + 1 || info.progress + work_done >= info.execution_time) {
                    policies.push_back(Saving{id});
                    info.state = 2;
                    info.state_end_time = time + PublicInformation::kSaving;
                    info.progress += work_done;
                }
            }
        }
    }
    
    // Update state and free resources
    for (task_id_t id = 0; id < all_tasks.size(); ++id) {
        auto& info = all_tasks[id];
        
        // Check if saving completed
        if (info.state == 2 && info.state_end_time == time) {
            available_cpus += info.current_cpus;
            info.state = 0;
            info.current_cpus = 0;
        }
    }
    
    // Create priority queue for scheduling
    struct TaskPriority {
        task_id_t id;
        double score;
        bool operator<(const TaskPriority& other) const {
            return score < other.score;
        }
    };
    
    std::vector<TaskPriority> candidates;
    
    for (task_id_t id = 0; id < all_tasks.size(); ++id) {
        auto& info = all_tasks[id];
        
        // Only consider free tasks that aren't complete
        if (info.state == 0 && info.progress < info.execution_time) {
            time_t remaining_time = info.deadline - time;
            
            if (remaining_time >= PublicInformation::kStartUp + PublicInformation::kSaving) {
                double urgency = 1.0 / (remaining_time + 1);
                double value = info.priority * urgency;
                candidates.push_back({id, value});
            }
        }
    }
    
    // Sort candidates by priority
    std::sort(candidates.begin(), candidates.end());
    std::reverse(candidates.begin(), candidates.end());
    
    // Launch tasks greedily
    for (const auto& candidate : candidates) {
        if (available_cpus <= 0) break;
        
        task_id_t id = candidate.id;
        auto& info = all_tasks[id];
        
        if (info.state != 0) continue;
        
        // Determine optimal CPU allocation
        cpu_id_t cpus_to_use = std::min(available_cpus, desc.cpu_count / 10);
        cpus_to_use = std::max((cpu_id_t)1, cpus_to_use);
        
        policies.push_back(Launch{cpus_to_use, id});
        info.state = 1;
        info.state_end_time = time + PublicInformation::kStartUp;
        info.current_cpus = cpus_to_use;
        available_cpus -= cpus_to_use;
    }

    return policies;
}

} // namespace oj
