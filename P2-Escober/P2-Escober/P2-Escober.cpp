#include <iostream> // i/o operations
#include <fstream> // file handing
#include <string> // std::string class and related functions
#include <sstream> // i/o operations for strings
#include <thread> // Thread library
#include <vector> // Vector to hold thread objects
#include <mutex> // for thread safe access to the prime vector
#include <chrono> // for sleep and time measurement
#include <random> // for random number generation
#include <iomanip> // for output formatting
#include <condition_variable> // for signaling between threads
#include <atomic> // for atomic operations
#include <algorithm> // for std::min

struct Instance {
    int id;
    bool active;
    int partiesServed;
    std::chrono::seconds totalTimeServed;

    Instance(int instanceId) : id(instanceId), active(false), partiesServed(0),
        totalTimeServed(std::chrono::seconds(0)) {}
};

std::vector<Instance> instances;
std::mutex instancesMutex;
std::mutex queueMutex;
std::condition_variable cv;
std::atomic<bool> shutdown(false);

int tanksAvailable;
int healersAvailable;
int dpsAvailable;

int maxInstances; // n
int minTime; // t1
int maxTime; // t2

void readConfig(int* n, int* t, int* h, int* d, int* t1, int* t2);
int getRandomClearTime();
bool canFormParty();
int maxPossibleParties();
void formParty();
int findAvailableInstance();
void displayStatus();
void runInstance(int instanceId);
void queueManager();
void displaySummary();


void readConfig(int* n, int* t, int* h, int* d, int* t1, int* t2) {
    // Open the config file
    std::ifstream configFile("config.txt");
    if (!configFile.is_open()) {
        std::cerr << "Error: Could not open config file!" << std::endl;
        return;
    }

    // Read the file line by line
    std::string line;
    while (std::getline(configFile, line)) {
        std::istringstream iss(line);
        std::string key;
        iss >> key; // Read the first word

        if (key == "max-num-instances") {
            iss >> *n;
            if (*n <= 0) {
                std::cerr << "Warning: Invalid value for max-num-instances in config file. Must be > 0." << std::endl;
                *n = 0; 
            }
        }
        else if (key == "num-tank") {
            iss >> *t;
            if (*t <= 0) {
                std::cerr << "Warning: Invalid value for num-tank in config file. Must be > 0." << std::endl;
                *t = 0; 
            }
        }
        else if (key == "num-healer") {
            iss >> *h;
            if (*h <= 0) {
                std::cerr << "Warning: Invalid value for num-healer in config file. Must be > 0." << std::endl;
                *h = 0; 
            }
        }
        else if (key == "num-dps") {
            iss >> *d;
            if (*d <= 0) {
                std::cerr << "Warning: Invalid value for num-dps in config file. Must be > 0." << std::endl;
                *d = 0; 
            }
        }
        else if (key == "min-time") {
            iss >> *t1;
            if (*t1 <= 0) {
                std::cerr << "Warning: Invalid value for min-time in config file. Must be > 0." << std::endl;
                *t1 = 0; 
            }
        }
        else if (key == "max-time") {
            iss >> *t2;
        }
    }

    if (*t1 >= *t2 && *t1 > 0 && *t2 > 0) {
        std::cerr << "Warning: min-time must be less than max-time in config file." << std::endl;
        *t2 = 0; 
    }

    // Close the file
    configFile.close();
}

int getRandomClearTime() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(minTime, maxTime);
    return dist(gen);
}

bool canFormParty() {
    std::lock_guard<std::mutex> lock(queueMutex);
    return (tanksAvailable >= 1 && healersAvailable >= 1 && dpsAvailable >= 3);
}

int maxPossibleParties() {
    std::lock_guard<std::mutex> lock(queueMutex);
    return std::min({ tanksAvailable, healersAvailable, dpsAvailable / 3 });
}

void formParty() {
    std::lock_guard<std::mutex> lock(queueMutex);
    tanksAvailable -= 1;
    healersAvailable -= 1;
    dpsAvailable -= 3;
}

int findAvailableInstance() {
    std::lock_guard<std::mutex> lock(instancesMutex);
    for (int i = 0; i < instances.size(); i++) {
        if (!instances[i].active) {
            return i;
        }
    }
    return -1; // No available instance
}

void displayStatus() {
    std::lock_guard<std::mutex> lock(instancesMutex);
    std::cout << "\n===== Current Instance Status =====" << std::endl;
    for (const auto& instance : instances) {
        std::cout << "Instance " << instance.id << ": "
            << (instance.active ? "active" : "empty") << std::endl;
    }

    {
        std::lock_guard<std::mutex> qLock(queueMutex);
        std::cout << "\nQueue Status:" << std::endl;
        std::cout << "Tanks: " << tanksAvailable << std::endl;
        std::cout << "Healers: " << healersAvailable << std::endl;
        std::cout << "DPS: " << dpsAvailable << std::endl;
        std::cout << "===============================" << std::endl;
    }
}

void runInstance(int instanceId) {
    int clearTime = getRandomClearTime();

    {
        std::lock_guard<std::mutex> lock(instancesMutex);
        instances[instanceId].active = true;
        std::cout << "\n> Party entering Instance " << instances[instanceId].id << std::endl;
    }

    displayStatus();

    std::this_thread::sleep_for(std::chrono::seconds(clearTime));

    {
        std::lock_guard<std::mutex> lock(instancesMutex);
        instances[instanceId].active = false;
        instances[instanceId].partiesServed++;
        instances[instanceId].totalTimeServed += std::chrono::seconds(clearTime);
        std::cout << "\n> Party completed Instance " << instances[instanceId].id << " in "
            << clearTime << " seconds" << std::endl;
    }

    cv.notify_all();
}

void queueManager() {
    std::vector<std::thread> instanceThreads;

    while (!shutdown) {
        if (canFormParty()) {
            // Get an instance ID while holding the mutex
            int instanceId = -1;
            {
                std::lock_guard<std::mutex> lock(instancesMutex);
                // Available instance
                for (int i = 0; i < static_cast<int>(instances.size()); i++) {
                    if (!instances[i].active) {
                        instanceId = i;
                        instances[i].active = true;  // Mark as active
                        break;
                    }
                }
            }

            if (instanceId != -1) {
                // Form a party and remove players from the queue
                formParty();

                instanceThreads.push_back(std::thread(runInstance, instanceId));
            }
            else {
                // Wait for an instance to become available
                std::unique_lock<std::mutex> lock(instancesMutex);
                cv.wait(lock, []() {
                    for (const auto& instance : instances) {
                        if (!instance.active) return true;
                    }
                    return false;
                });
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Check if no parties can form
            if (!canFormParty()) {
                // Check if any instances are still active
                bool anyActive = false;
                {
                    std::lock_guard<std::mutex> lock(instancesMutex);
                    for (const auto& instance : instances) {
                        if (instance.active) {
                            anyActive = true;
                            break;
                        }
                    }
                }

                // Only shut down if no active instances and no parties can form
                if (!anyActive) {
                    shutdown = true;
                }
            }
        }
    }

    // Join all instance threads before exiting
    for (auto& t : instanceThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void displaySummary() {
    std::lock_guard<std::mutex> lock(instancesMutex);
    std::cout << "\n===== Instance Summary =====" << std::endl;
    for (const auto& instance : instances) {
        std::cout << "Instance " << instance.id << ":" << std::endl;
        std::cout << "  Parties served: " << instance.partiesServed << std::endl;
        std::cout << "  Total time served: " << instance.totalTimeServed.count() << " seconds" << std::endl;
    }

    int totalParties = 0;
    std::chrono::seconds totalTime(0);
    for (const auto& instance : instances) {
        totalParties += instance.partiesServed;
        totalTime += instance.totalTimeServed;
    }

    std::cout << "\nOverall Summary:" << std::endl;
    std::cout << "  Total parties served: " << totalParties << std::endl;
    std::cout << "  Total time served across all instances: " << totalTime.count() << " seconds" << std::endl;

    {
        std::lock_guard<std::mutex> qLock(queueMutex);
        std::cout << "\nLeftover Players:" << std::endl;
        std::cout << "  Tanks: " << tanksAvailable << std::endl;
        std::cout << "  Healers: " << healersAvailable << std::endl;
        std::cout << "  DPS: " << dpsAvailable << std::endl;

        int maxPossibleParties = std::min({ tanksAvailable, healersAvailable, dpsAvailable / 3 });
        if (maxPossibleParties > 0) {
            std::cout << "  Note: " << maxPossibleParties << " more parties could have been formed," << std::endl;
            std::cout << "        but there weren't enough instances available." << std::endl;
        }
        else {
            int totalLeftover = tanksAvailable + healersAvailable + dpsAvailable;
            if (totalLeftover > 0) {
                std::cout << "  These players couldn't form complete parties due to role imbalance." << std::endl;
            }
            else {
                std::cout << "  No leftover players - all players were assigned to parties." << std::endl;
            }
        }
    }

    std::cout << "===============================" << std::endl;
}

int main() {
    int n = 0; // Max num of concurrent instances
    int t = 0; // num of tank players in queue
    int h = 0; // num of healer players in the queue
    int d = 0; // num of DPS players in the queue
    int t1 = 0; // min time before an instance is finished
    int t2 = 0; // max time before an instance is finished

    readConfig(&n, &t, &h, &d, &t1, &t2);

    while (n <= 0) {
        std::cout << "Enter maximum number of concurrent instances (n, must be > 0): ";
        std::cin >> n;
        if (n <= 0) std::cout << "Error: n must be greater than 0." << std::endl;
    }

    while (t <= 0) {
        std::cout << "Enter number of tank players in the queue (t, must be > 0): ";
        std::cin >> t;
        if (t <= 0) std::cout << "Error: t must be greater than 0." << std::endl;
    }

    while (h <= 0) {
        std::cout << "Enter number of healer players in the queue (h, must be > 0): ";
        std::cin >> h;
        if (h <= 0) std::cout << "Error: h must be greater than 0." << std::endl;
    }

    while (d <= 0) {
        std::cout << "Enter number of DPS players in the queue (d, must be > 0): ";
        std::cin >> d;
        if (d <= 0) std::cout << "Error: d must be greater than 0." << std::endl;
    }

    while (t1 <= 0) {
        std::cout << "Enter minimum time before an instance is finished (t1, must be > 0): ";
        std::cin >> t1;
        if (t1 <= 0) std::cout << "Error: t1 must be greater than 0." << std::endl;
    }

    while (t2 <= t1) {
        std::cout << "Enter maximum time before an instance is finished (t2, must be > t1): ";
        std::cin >> t2;
        if (t2 <= t1) std::cout << "Error: t2 must be greater than t1 (" << t1 << ")." << std::endl;
    }

    if (t2 > 15) {
        std::cout << "Warning: t2 exceeds maximum allowed value (15). Setting t2 to 15." << std::endl;
        t2 = 15;
    }

    maxInstances = n;
    minTime = t1;
    maxTime = t2;
    tanksAvailable = t;
    healersAvailable = h;
    dpsAvailable = d;

    // Display the input values
    std::cout << "\nInput Values:" << std::endl;
    std::cout << "Maximum number of concurrent instances (n): " << n << std::endl;
    std::cout << "Number of tank players in the queue (t): " << t << std::endl;
    std::cout << "Number of healer players in the queue (h): " << h << std::endl;
    std::cout << "Number of DPS players in the queue (d): " << d << std::endl;
    std::cout << "Minimum time before an instance is finished (t1): " << t1 << std::endl;
    std::cout << "Maximum time before an instance is finished (t2): " << t2 << std::endl;

    // Initialize instances
    for (int i = 0; i < maxInstances; i++) {
        instances.push_back(Instance(i + 1));
    }

    displayStatus();

    std::thread managerThread(queueManager);

    // Wait for all processing to finish
    managerThread.join();

    // Display the final summary
    displaySummary();

    return 0;
}