#include "ros/ros.h"
#include "json_prolog_msgs/PrologQuery.h"
#include "json_prolog_msgs/PrologFinish.h"
#include "json_prolog_msgs/PrologNextSolution.h"
#include "cpl_interface.h"
#include "ros/package.h"
#include "time.h"
#include <thread>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>

// data structure to keep track of all incoming queries, we use unorderd_map for cheaper inserts/accesses
// map: considers ordering, O(log n) for insert/access
// unordered_map: no ordering, O(1) for insert/access
std::unordered_map <std::string, PrologQuery> queries; // shared set of queries

std::condition_variable cv_loop;
std::condition_variable cv_work; // not necessary

// shared mutex, used by query threads
std::mutex push_lock;

// global access to the prolog interface instance
PrologInterface *plIfaceGlobal=NULL;

bool query(json_prolog_msgs::PrologQuery::Request &req,
           json_prolog_msgs::PrologQuery::Response &res) {

    // case id already exists in queries
    if (queries.count(req.id) > 0) {
        res.ok = false;
        res.message = "Another query is already being processed with id" + req.id;
    } else {
        // build a query from the requirements
        PrologQuery queryObj;
        std::string query = req.query;
        // set_values(mode, id, query, msg, ok)
        queryObj.set_values(req.mode, req.id, query, "", true );

        typedef std::unordered_map<std::string, PrologQuery>::iterator UOMIterator;
        std::pair<UOMIterator, bool> insertionPair;

        push_lock.lock();
        insertionPair = queries.insert({req.id, queryObj}); // synchronized push of the query to the shared map of queries
        push_lock.unlock();

        // set the query from the insertion iterator, so we don't need to do a lookup in the map
        queryObj = insertionPair.first->second;

        int mode = req.mode;
        std::string id = req.id;
        std::string query_string = req.query;

        plIfaceGlobal->PrologInterface::push_query(mode, id, query_string);

        res.ok = true; //query.get_ok();
        res.message = ""; //query.get_message();

        return true;
    }
}

bool finish(json_prolog_msgs::PrologFinish::Request &req,
            json_prolog_msgs::PrologFinish::Response &res) {
    //res.sum = req.a + req.b;
//  ROS_INFO("request: x=%ld, y=%ld", (long int)req.a, (long int)req.b);
//  ROS_INFO("sending back response: [%ld]", (long int)res.sum);
    return true;
}

bool next_solution(json_prolog_msgs::PrologNextSolution::Request &req,
                   json_prolog_msgs::PrologNextSolution::Response &res) {
    //res.sum = req.a + req.b;
//  ROS_INFO("request: x=%ld, y=%ld", (long int)req.a, (long int)req.b);
//  ROS_INFO("sending back response: [%ld]", (long int)res.sum);
    return true;
}

void pl_threaded_call(const std::string input) {
    ROS_INFO(input.c_str());
    std::string goal = input + ", Id, []"; // for "write(1) eg. thread_create(write(1), Id, [])

    PlTerm argv(input.c_str());
    try {
        //PlQuery q("cpl_proof_of_concept", argv);
        PlCall ("thread_create", argv);
    }
    catch (PlException &ex) {
        ROS_INFO("Prolog test call went wrong. You're not there yet, pal.");
    }

//    PREDICATE(list_modules, 0)
//    { PlTermv av(1);
//
//        PlQuery q("current_module", av);
//        while( q.next_solution() )
//            cout << (char *)av[0] << endl;
//
//        return TRUE;
//    }
}

void PrologInterface::push_query(int mode, std::string id, std::string query){

    has_queries_to_process = true;
    cv_loop.notify_one();
    ROS_INFO("");
}

void PrologInterface::pop_query() {}

void PrologInterface::init() {

    PlTerm av("knowrob_common");
    try {
        PlCall("register_ros_package", av);
    }
    catch (PlException &ex) {
        ROS_INFO((char *) ex);
    }
}

void PrologInterface::loop() {
    while(1){
        {
            std::unique_lock<std::mutex> lk(loop_lock);
            // queries available?
            while(!queries.empty()) {
                //ROS_INFO("I GOT HERE *****");
                std::string query_string(queries.begin()->second.get_query());

                // take first query from list, get value (the query) and get the query string
                pl_threaded_call(query_string);
            }
            cv_loop.wait(lk, [this]{ return has_queries_to_process; });
            has_queries_to_process = false;
            }
        }
    }

PrologInterface::PrologInterface() :
    thread(&PrologInterface::loop, this){

    ROS_INFO("Invoking prolog engine");
    char *argv[4];
    int argc = 0;
    argv[argc++] = (char*)"PrologEngine"; // cast only to solve warnings from the compiler
    argv[argc++] = (char*)"-f";
    std::string rosPrologInit = ros::package::getPath("rosprolog") + "/prolog/init.pl";
    argv[argc] = new char[rosPrologInit.size() + 1];
    std::copy(rosPrologInit.begin(), rosPrologInit.end(), argv[argc]);
    argv[argc++][rosPrologInit.size()] = '\0';
    argv[argc] = NULL;
    engine = std::make_shared<PlEngine>(argc, argv);
    init();
}

void PrologQuery::set_values(int p_mode, const std::string &p_id,
                             const std::string &p_query, const std::string &p_message, bool p_ok) {
    mode = p_mode;
    id = p_id;
    query = p_query;
    message = p_message;
    ok = p_ok;
}

int main(int argc, char **argv) {
    ros::init(argc, argv, "cpl_interface_node");
    ros::NodeHandle n;

    // TODO:REMOVE ROS_INFO("PASSED MAIN");
    // create the thread handler, that checks for queries and passes them to prolog
    PrologInterface prologInterface;
    plIfaceGlobal = &prologInterface;

    ros::ServiceServer service_query = n.advertiseService("query", query);
    ros::ServiceServer service_next_solution = n.advertiseService("next_solution", next_solution);
    ros::ServiceServer service_finish = n.advertiseService("finish", finish);
    ros::spin();

    return 0;
}