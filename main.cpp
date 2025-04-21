#include <iostream>
#include <fstream>
#include <unistd.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <thread>
#include <chrono>//multithreading and timing operations.
#include <sys/types.h>
#include <sys/stat.h>//For system-level data types and file status.
#include <dirent.h>//For directory operations 
#include <sstream>

using namespace std;

// =================== MEMORY MONITORING =================== //
string get_memory_usage_kb(int pid){
    ifstream status_file("/proc/" + to_string(pid) + "/status");//Open the file "/proc/[pid]/status" for reading.
    string line;
    while (getline(status_file, line)){//read each line
        if(line.find("VmRSS:") != string::npos){//VmRSS stands for "Virtual Memory Resident Set Size," which tells us how much memory the process is using.
            return line;
        }
    }
    return "VmRSS: N/A";
}

int extract_kb(const string& line){
    size_t start = line.find_first_of("0123456789");//ind the position of the first numeric character (0-9) in the string `line`.
        //this helps locate where the memory usage value starts in the line.
    if (start != string::npos){//numeric character exists in the string.
        return stoi(line.substr(start));//convert to int
    }
    return 0;
}

void create_zombie_process(){
    pid_t pid = fork();
    if (pid == -1){
        cerr << "Fork didnt work!" << endl;
        exit(1);
    }

    if (pid == 0) {
        // Child process: exit immediately (this will become a zombie)
        exit(0);  // Child process exits here, becomes a zombie
    } else {
        sleep(5);  // Sleep for 5 secs to allow child process to become a zombie
    }
}


// =================== ZOMBIE PROCESS DETECTION =================== //
void detect_and_handle_zombies(){
    DIR* dir = opendir("/proc");//open proc
    if (dir == nullptr){
        cerr << "could not open directory.\n";
        return;
    }

    bool zombie_found =false;
    struct dirent* entry;

    while ((entry = readdir(dir)) != nullptr){//loop through dir
        string pid =entry->d_name;
        if (pid.find_first_not_of("0123456789") != string::npos){
        continue;//skip things that arent numeric.
}
        string status_file = "/proc/" + pid + "/status";
        ifstream status(status_file);//open status file for reading
        string line;
        bool is_zombie = false;

        while (getline(status, line)){
            if (line.find("State:") != string::npos && line.find("Z") != string::npos) {//finding "Z" whihc indicates process
                is_zombie = true;
                break;
            }
        }

        if (is_zombie){
            zombie_found = true;
            cout<<"\n Zombie process detected: PID " << pid << endl;
            string stat_file = "/proc/" + pid + "/stat";
            ifstream stat_stream(stat_file);//open stat file which contains pid and state
            string stat_content;
            getline(stat_stream, stat_content);

            istringstream iss(stat_content);//iss allows to split the string
            vector<string> tokens;
            string token;
            while (iss >> token) {//since the stat is a whole line of different content, we split them into tokens.
                tokens.push_back(token);
            }
            if (tokens.size() >= 4) {//there are going to be atleast 4 tokens so we need to get the 3rd for ppid
                string parent_pid = tokens[3];
                cout << "Parent PID: " << parent_pid << endl;
                string command = "kill -9 " + parent_pid;
                int result = system(command.c_str());
                if (result == 0) {
                    cout << "Successfully sent SIGKILL to Parent PID " << parent_pid << endl;
                } else {
                    cout<< "Failed to send SIGKILL to Parent PID " << parent_pid << endl;
                }
            }
            else{
                cout<< "could not parse parent PID for process " << pid << endl;
            }
        }
    }
    closedir(dir);
    if (!zombie_found) {
        cout << "No zombie processes found." << endl;
    }
}


// =================== GNUPLOT WRAPPER =================== //
void plot_memory_graph(const string& data_filename, const string& output_image, const string& title) {
    FILE* gnuplotPipe = popen("/usr/bin/gnuplot -persistent", "w");//presistent means that the graph will be up even after teh script finishes.
    //this all will execute if the /usr/bin/gnuplot opens(where its installed).
    if (gnuplotPipe){//check if file open
        fprintf(gnuplotPipe, "set terminal png\n");
        fprintf(gnuplotPipe, "set output '%s'\n", output_image.c_str());
        fprintf(gnuplotPipe, "set title '%s'\n", title.c_str());
        fprintf(gnuplotPipe, "set xlabel 'Time (s)'\n");
        fprintf(gnuplotPipe, "set ylabel 'Memory (kB)'\n");
        fprintf(gnuplotPipe, "plot '%s' using 1:2 with linespoints title 'VmRSS'\n", data_filename.c_str());
        pclose(gnuplotPipe);
        system(("xdg-open " + output_image).c_str()); // Optional (Linux only)
    } else {
        cout<<" Could not open Gnuplot.\n";
    }
}

// =================== LIVE MEMORY MONITORING =================== //
void monitor_memory_live(int duration_sec = 10, int interval_sec = 1) {
    vector<int> timestamps, memory_usage;//store timestamps and memory usage data.
    int pid = getpid();

    cout << "Monitoring memory for " << duration_sec << " seconds...\n";

    for (int t = 0; t < duration_sec; t += interval_sec) {//will run for the secs mentioned in the parameter.
        string mem_line = get_memory_usage_kb(pid);//getting the memory usage of the process id
        int mem_kb = extract_kb(mem_line);//extracting the kbs of the memory usage we got.
        timestamps.push_back(t);//adding it to the back of the timestamps
        memory_usage.push_back(mem_kb);
        cout << "[t=" << t << "s] " << mem_line << endl;
        this_thread::sleep_for(chrono::seconds(interval_sec));//bringing a delay between each execution before executing next
    }

    string filename = "mem_data.txt";//adding data to the file for plotting.
    ofstream data_file(filename);
    for (size_t i = 0; i < timestamps.size(); ++i) {
        data_file << timestamps[i] << " " << memory_usage[i] << endl;
    }
    data_file.close();

    plot_memory_graph(filename, "mem_plot.png", "Memory Usage Over Time");
}

// =================== MEMORY LEAK SIMULATION =================== //
vector<int*> leak_vector;//to store leak values

void simulate_memory_leak(int block_count = 10, int interval_sec = 1, bool cleanup = true) {
    vector<int> timestamps, memory_usage;
    int pid = getpid();

    for (int i = 0; i < block_count; ++i) {
        int* leak = new int[100000];  //~400 KB/block -> 10000*4byte -> 400,000 -> 400,000/1024 -> 390 -> 400 aprox
        leak_vector.push_back(leak);

        string mem_line = get_memory_usage_kb(pid);//Get the current memory usage of the process in kilobytes (KB).
        int mem_kb = extract_kb(mem_line);//extracts the KB value from the string.
        timestamps.push_back(i * interval_sec);//record time stamp and push it at the end of the array
        memory_usage.push_back(mem_kb);
        
        cout << "[t=" << i * interval_sec << "s] Memory: " << mem_kb << " KB\n";
        this_thread::sleep_for(chrono::seconds(interval_sec));//give delay between each execution.
    }

        //======= ANALYSIS =======
    int increases = 0;//counter
    int flat_or_down = 0;
    int leak_threshold_kb = 100; 
    for (size_t i = 1; i < memory_usage.size(); ++i) {
        int delta = memory_usage[i] - memory_usage[i - 1];// calculated diff between memory usage.
        if (delta>8){//if mem increased then add the increase instance counter.
        increases++;
        }
        else{
        flat_or_down++;//if not increase then ++ the flat counter
        }
    }
    int net_growth = memory_usage.back() - memory_usage.front();//total net growth of simulation
    cout<<"\n=== [ANALYSIS] Memory Leak ===\n";
    cout<<"Initial: " << memory_usage.front() << " KB | Final: " << memory_usage.back() << " KB\n";
    cout<<"Net growth: " << net_growth << " KB\n";
    cout<<"Intervals growing: " << increases << ", stable/dropping: " << flat_or_down << "\n";

   
    /*// Condition Explanation:
     1. net_growth > leak_threshold_kb:
        This checks whether the total memory growth exceeds the predefined threshold (`leak_threshold_kb`).
       A high net growth indicates that the memory usage has increased significantly over time,
          which could be a sign of a memory leak.
    */
    if(net_growth > leak_threshold_kb) {
        cout << "RESULT -> memory usage increased... its not been deallocated...predicting memory loss if memory not freed.\n";
    }
    else{
        cout<<"RESULT -> No  leak detected.\n";
    }

    // ======= SAVE & PLOT =======
    string filename = "leak_data.txt";
    ofstream data_file(filename);
    for (size_t i = 0; i < timestamps.size(); ++i) {
        data_file << timestamps[i] << " " << memory_usage[i] << endl;
    }
    data_file.close();
    plot_memory_graph(filename, "leak_plot.png", "Memory Leak Simulation");

    if (cleanup) {
        for (auto ptr : leak_vector) {//loop iterates over all pointers stored
            delete[] ptr;
        }
        leak_vector.clear();
        cout << "Memory cleaned up.\n";
    }
}

// =================== FILE CRASH SIMULATION =================== //
void simulate_file_crash() {
    string file_name = "file_data.txt";
    string log_file = "recovery_log.txt";
    string data;

    cin.ignore();
    cout << "Enter data to write before crash: ";
    getline(cin, data);

    ofstream log(log_file, ios::app);
    log << "Data: " << data << endl;
    log.close();

    exit(1);
}

void recover_file_data() {
    string file_name = "file_data.txt";
    string log_file = "recovery_log.txt";
    ifstream log(log_file);
    string line, last_data = "";

    while (getline(log, line)) {
        size_t pos = line.find("Data:");
        if (pos != string::npos) {
            last_data = line.substr(pos + 6);
        }
    }
    log.close();

    if (!last_data.empty()) {
        ofstream file(file_name);
        file << last_data;
        file.close();
        cout << "Recovered file with last data: " << last_data << "\n";
    } else {
        cout << "No data found to recover.\n";
    }
}



float get_cpu_usage() {
    string line;
    ifstream file("/proc/stat");//open proc stat file which contains ->  CPU statistics
    if (getline(file, line)) {
        size_t user, nice, system, idle, iowait, irq, softirq;
        sscanf(line.c_str(), "cpu  %lu %lu %lu %lu %lu %lu %lu", &user, &nice, &system, &idle, &iowait, &irq, &softirq);
        
        /* CPU time component:
          user: Time spent running normal-priority user processes (e.g -> your applications).
          nice: Time spent running low-priority (nice) user processes.
          system: Time spent running kernel-level tasks (e.g., handling system calls).
          idle: Time spent doing nothing (CPU is idle).
          iowait: Time spent waiting for I/O operations (e.g., disk or network activity).
          irq: Time spent handling hardware interrupts (e.g., keyboard/mouse input).
          softirq: Time spent handling software interrupts (e.g., kernel timers).*/
        
        long total = user + nice + system + idle + iowait + irq + softirq;//total cpu time
        long idle_time = idle + iowait;//idle time

        static long prev_total = total;
        static long prev_idle = idle_time;

        long total_diff = total - prev_total;
        long idle_diff = idle_time - prev_idle;
        prev_total = total;
        prev_idle = idle_time;
        /*do comparisions with the previous time values*/

        if (total_diff > 0) {
            return 100.0f * (1.0f - (float)idle_diff / total_diff);//CPU usage = 100% - idle percentage
        }
    }

    return 0.0f;
}

// Plot CPU Usage Graph
void plot_cpu_usage_graph(const string& data_filename, const string& output_image, const string& title) {
    FILE* gnuplotPipe = popen("/usr/bin/gnuplot -persistent", "w");
    if (gnuplotPipe) {
        // Correct the format specifier
        fprintf(gnuplotPipe, "set terminal png\n");
        fprintf(gnuplotPipe, "set output '%s'\n", output_image.c_str());
        fprintf(gnuplotPipe, "set title '%s'\n", title.c_str());
        fprintf(gnuplotPipe, "set xlabel 'Time (s)'\n");
        fprintf(gnuplotPipe, "set ylabel 'CPU Usage (%%)'\n"); // Corrected percentage symbol handling
        fprintf(gnuplotPipe, "plot '%s' using 1:2 with linespoints title 'CPU Usage'\n", data_filename.c_str());
        pclose(gnuplotPipe);
        system(("xdg-open " + output_image).c_str()); // Optional (Linux only)
    } else {
        cerr << "Error: Could not open Gnuplot.\n";
    }
}


void monitor_cpu_usage_live(int duration_sec = 10, int interval_sec = 1) {
    vector<int> timestamps;
    vector<float> cpu_usage;

    cout << "Monitoring CPU usage for " << duration_sec << " seconds...\n";
    for (int t = 0; t < duration_sec; t += interval_sec) {
        float cpu = get_cpu_usage();
        timestamps.push_back(t);
        cpu_usage.push_back(cpu);
        cout << "[t=" << t << "s] CPU Usage: " << cpu << "%" << endl;
        this_thread::sleep_for(chrono::seconds(interval_sec));
    }

    // Save data to a file
    string filename = "cpu_usage_data.txt";
    ofstream data_file(filename);
    for (size_t i = 0; i < timestamps.size(); ++i) {
        data_file << timestamps[i] << " " << cpu_usage[i] << endl;
    }
    data_file.close();

    // Plot the CPU usage graph
    plot_cpu_usage_graph(filename, "cpu_usage_plot.png", "CPU Usage Over Time");
}

// =================== MAIN =================== //
int main() {
    int choice;
    cout << "\n--- OS SYSTEM ---\n";
    cout << "1. Monitor Memory Usage Live\n";
    cout << "2. Simulate File Crash\n";
    cout << "3. Recover File Data\n";
    cout << "4. Detect and Handle Zombie Processes\n";
    cout << "5. Monitor CPU Usage Live\n";
    cout << "6. Exit\n";
    cout << "Enter your choice: ";
    cin >> choice;

    switch (choice) {
        case 1:
        {
    srand(time(nullptr));
    int random_choice = rand() % 2;

    if (random_choice == 0) {
        monitor_memory_live();
    } else {
        simulate_memory_leak();
    }
    break;
}

        case 2:
                    simulate_file_crash();
            break;

        case 3:
                    recover_file_data();
            break;

        case 4:
                    create_zombie_process();
            detect_and_handle_zombies();
            break;

        case 5:
                    monitor_cpu_usage_live();
            break;

        case 6:
                    cout << "Exiting in 3..2..1.. exited successfully\n";
            return 0;
        default:
            cout << "Invalid option.\n";
    }

    return 0;
}

