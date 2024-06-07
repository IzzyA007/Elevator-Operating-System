#include <iostream>
#include <fstream>
#include <string>
#include <pthread.h>
#include <curl/curl.h>
#include <vector>
#include <mutex>
#include <unistd.h>
#include <queue>
#include <sstream>
#include <algorithm>
#include <atomic>


using namespace std;




/////////////////////////////////////// Struct Section
enum Direction { UP, DOWN, IDLE };

struct Elevator {
    string id;
    int Lowest_F;
    int highestFloor;
    int currentFloor;
    vector<int> stops;
    Direction direction;
    bool inService;
    int capacity;

    Elevator() : id(""), Lowest_F(0), highestFloor(0), currentFloor(0), direction(IDLE), inService(true), capacity(0) {}
    Elevator(string _id, int _lowest_Fr, int _highestFloor, int _currentFloor, int _capacity)
            : id(_id), Lowest_F(_lowest_Fr), highestFloor(_highestFloor), currentFloor(_currentFloor), direction(IDLE), inService(true), capacity(_capacity) {}
};



struct Request {
    string personID;
    int startFloor;
    int destinationFloor;
    time_t requestTime;


    Request(string _personID, int _startFloor, int _destinationFloor)
            : personID(_personID), startFloor(_startFloor), destinationFloor(_destinationFloor), requestTime(time(nullptr)) {}
};


vector<Elevator> elevators;
int lastElevatorIndex = 0;

struct  Building {
    int id;
    string name;
    //vector<Elevator> elevators;

    Building(int _id, string _name) : id(_id), name(_name) {}
    void addElevator(const Elevator& elevator) {
        elevators.push_back(elevator);
    }
};




std::mutex cmdmtx;
std::mutex responsemtx;
std::queue<std::string> commandQueue;













static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}




void makeApiRequest(const string& url, const string& method, string& responseBuffer, const string& postData = "", const vector<string>& customHeaders = {}) {
    CURL *curl;
    CURLcode res;

    responseBuffer.clear();

    struct curl_slist *headers = NULL;

    for (const string& header : customHeaders) {
        headers = curl_slist_append(headers, header.c_str());
    }

    curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());

        if (method == "PUT" || method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str()); 
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
        } else {

            cout << "Response from API: " << responseBuffer << endl;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}










void startSimulation() {
    const string url = "http://localhost:5432/Simulation/start";
    string responseBuffer;
    makeApiRequest(url, "PUT", responseBuffer);

    if (responseBuffer.find("Simulation started") != string::npos ||
        responseBuffer.find("Simulation is already running") != string::npos) {

        cout << "Simulation started successfully." << endl;
    } else {
        cerr << "Failed to start simulation: " << responseBuffer << endl;
    }
}







void parseElevatorStatus(const string& response, Elevator& elevator) {
    stringstream ss(response);
    string item;
    vector<string> results;

    while (getline(ss, item, '|')) {
        results.push_back(item);
    }

    if (results.size() >= 4) {
        elevator.currentFloor = stoi(results[1]);
        elevator.direction = results[2] == "UP" ? UP : results[2] == "DOWN" ? DOWN : IDLE;

    }
}



void updateElevators() {
    if (elevators.empty()) {
        cerr << "No elevators available in the system." << endl;
        return;
    }

    for (auto& elevator : elevators) {
        string responseBuffer;
        string requestUrl = "http://localhost:5432/ElevatorStatus/" + elevator.id;
        makeApiRequest(requestUrl, "GET", responseBuffer);

        if (responseBuffer.empty()) {
            cerr << "Failed to get a response for elevator ID " << elevator.id << endl;
            continue;
        }

        parseElevatorStatus(responseBuffer, elevator);


        cout << "Elevator " << elevator.id << " is now at floor " << elevator.currentFloor << endl;
    }
}


int calculateCost(const Elevator& elevator, int startFloor, int destFloor, Direction reqDirection) {
    if (startFloor < elevator.Lowest_F || destFloor > elevator.highestFloor)
        return INT_MAX;

    if (elevator.direction != IDLE && elevator.direction != reqDirection)
        return INT_MAX;

    int distanceCost = abs(elevator.currentFloor - startFloor);
    int stopPenalty = elevator.stops.size() * 5;

    return distanceCost + stopPenalty;
}




string selectElevator(int startFloor, int destinationFloor) {
    int closestDistance = INT_MAX;
    string selectedElevatorID = "";

    for (const auto& elevator : elevators) {

        if (destinationFloor >= elevator.Lowest_F && destinationFloor <= elevator.highestFloor) {
            int distance = abs(elevator.currentFloor - startFloor);
            if (distance < closestDistance) {
                closestDistance = distance;
                selectedElevatorID = elevator.id;
            }
        }
    }
    return selectedElevatorID;
}






void assignPersonToElevator(string personID, string elevatorID, int startFloor, int destFloor) {
    for (auto& elevator : elevators) {
        if (elevator.id == elevatorID) {
            if (elevator.direction == IDLE ||
                (elevator.direction == UP && startFloor <= elevator.currentFloor && destFloor >= elevator.currentFloor) ||
                (elevator.direction == DOWN && startFloor >= elevator.currentFloor && destFloor <= elevator.currentFloor)) {

                elevator.stops.push_back(startFloor);
                elevator.stops.push_back(destFloor);
                sort(elevator.stops.begin(), elevator.stops.end());
            } else {
                cerr << "Person ID " << personID << " cannot be assigned to Elevator ID " << elevator.id
                     << " because their request is in the opposite direction of the elevator." << endl;
            }
            break;
        }
    }
}




Elevator& findElevatorById(const string& elevatorID) {
    for (auto& elevator : elevators) {
        if (elevator.id == elevatorID) {
            return elevator;
        }
    }
    throw std::runtime_error("Elevator with ID " + elevatorID + " not found");
}





void addRequest(const std::string& request) {

    if (request == "NONE") {
        cout << "No more inputs to process." << endl;
        return;
    }

    stringstream ss(request);
    string personID;
    int startFloor, destinationFloor;
    char delimiter;

    if (!(getline(ss, personID, '|') && ss >> startFloor >> delimiter && ss >> destinationFloor)) {
        cerr << "Error: Failed to parse request: " << request << endl;
        return;
    }

    string elevatorID = selectElevator(startFloor, destinationFloor);
    Elevator& selectedElevator = findElevatorById(elevatorID);

    if (startFloor >= selectedElevator.Lowest_F && destinationFloor <= selectedElevator.highestFloor) {

        stringstream command;
        command << "/AddPersonToElevator/" << personID << "/" << selectedElevator.id;
        {
            std::lock_guard<std::mutex> cmdLock(cmdmtx);
            commandQueue.push(command.str());
        }
        cout << "Command added to commandQueue: " << command.str() << endl;
    } else {
        cerr << "Person ID " << personID << " cannot be assigned to Elevator ID " << selectedElevator.id
             << " because their end floor is not within the elevator's range." << endl;
    }
}

/////////////////////////////////////////////////////////////////////////////
















////////////////////////////////////////////////////////////////////////////







/////////////////////////////////////////




//////////////////////////////////////// Get command



void updateElevatorStatus(string elevatorId, int currentFloor, Direction direction) {
    for (auto& elevator : elevators) {
        if (elevator.id == elevatorId) {
            elevator.currentFloor = currentFloor;
            elevator.direction = direction;
            break;
        }
    }
}








///////////////////////////////////////////////////// Process request



////////////////////////////////////////////////////  API Callback Function



/////////////////////////////////////////////////     Parse Bulding Config
void parseBuildingConfiguration(ifstream& file) {
    string line;
    while (getline(file, line)) {
        stringstream ss(line);
        string buildingName, item;
        vector<string> fields;


        while (getline(ss, item, '\t')) {
            fields.push_back(item);
        }

        if (fields.size() != 5) {
            cerr << "Error parsing line: Incorrect number of fields" << endl;
            continue;
        }

        buildingName = fields[0];
        int lowestFloor = stoi(fields[1]);
        int highestFloor = stoi(fields[2]);
        int initialFloor = stoi(fields[3]);
        int capacity = stoi(fields[4]);


        if (initialFloor < lowestFloor || initialFloor > highestFloor) {
            cerr << "Initial floor out of range for " << buildingName << endl;
            continue;
        }


        Building building(0, buildingName);


        Elevator elevator(buildingName, lowestFloor, highestFloor, initialFloor, capacity);
        building.addElevator(elevator);


        // buildings.push_back(building);


        cout << "Building added: " << buildingName
             << ", Elevator ranges from floor " << lowestFloor << " to " << highestFloor
             << ", starts at floor " << initialFloor
             << ", with capacity " << capacity << endl;
    }
}

////////////////////////////////////////////////////// Parse Elevator Config



////////////////////////////////////////////////////// update Elavator Stats




////////////////////////////////////////////////////  Make API Request functions


///////////////////////////////////////// Thread Section

std::atomic<bool> runThreads(true);

void* inputThread(void* arg) {

    const string url = "http://localhost:5432/NextInput";
    string readBuffer;

    while (runThreads) {

        //cout  << "inside inputthread" << endl;


        //makeApiRequest(url, "GET", readBuffer);
        makeApiRequest(url, "GET", readBuffer, "", {});

        cout << "[input]: Received from API: " << readBuffer << endl;
        if (!readBuffer.empty() && readBuffer != "NONE") {
            addRequest(readBuffer);
            cout << "[input]: Input Thread: added command" << endl;
        } else {
            cout << "[input]: No new data or 'NONE' received. Waiting to retry..." << endl;
        }
        sleep(1);
        updateElevators();
    }

    return NULL;
}



void* outputThread(void* arg) {
    while (runThreads) {
        string command;
        bool cmdEmpty;
        {
            std::lock_guard<std::mutex> lock(cmdmtx);
            cmdEmpty = commandQueue.empty();
        }
        if (!cmdEmpty) {
            {
                std::lock_guard<std::mutex> lock(cmdmtx);
                command = commandQueue.front();
                commandQueue.pop();
            }
            cout << "[output]: Output Thread: Sending command - " << command << endl;

            string responseBuffer;
            cout << "[output]: http://localhost:5432" + command << endl;
            makeApiRequest("http://localhost:5432" + command, "PUT", responseBuffer);
            cout << "[output]: API Response: " << responseBuffer << endl;
        } else {
            cout << "[output]: Output Thread: Waiting for commands..." << endl;
            sleep(1);
        }
    }

    return NULL;
}







///////////////////////////////////////////////// Start Simulation



///////////////////////////////////////////////// Check Elevator Status






///////////////////////////////////////////////// Check Simulation Status
bool checkSimulationStatus() {
    const string url = "http://localhost:5432/Simulation/check";
    string responseBuffer;
    makeApiRequest(url, "GET", responseBuffer);
    if (responseBuffer.find("Simulation is running") != string::npos) {
        return true;
    }
    cerr << "Simulation is not running: " << responseBuffer << endl;
    return false;
}



//////////////////////////////////////////////// Main function logic


int main(int argc, char** argv) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <path_to_building_file>" << endl;
        return 1;
    }

    ifstream file(argv[1]);
    if (!file) {
        cerr << "Error: File cannot be opened." << endl;
        return 1;
    }


    startSimulation();
    int retryCount = 5;
    while (!checkSimulationStatus() && retryCount-- > 0) {
        startSimulation();
        sleep(2);
    }
    if (retryCount <= 0) {
        cerr << "Failed to start or verify the simulation status." << endl;
        return 1;
    }

    cout << "Simulation is active. Configuring the building..." << endl;
    parseBuildingConfiguration(file);
    file.close();


    pthread_t t1, t2;
    pthread_create(&t1, NULL, inputThread, NULL);
    pthread_create(&t2, NULL, outputThread, NULL);

    runThreads = true;


    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
//
//    // Keep the main thread alive until all threads have finished
//    //while (true) {
//        if (!runThreads) {
//            cout << "All threads have finished. Exiting main thread." << endl;
//            break;
//        }
//        sleep(1); // Adjust timing based on your needs
//    //}


    return 0;
}


