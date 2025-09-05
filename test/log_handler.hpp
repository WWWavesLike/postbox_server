#ifndef LOG_HANDLER_HPP
#define LOG_HANDLER_HPP
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace std;

class log_data {
protected:
	string role;
	string cid;
	string rid;
	time_t log_time = std::chrono::system_clock::to_time_t(chrono::system_clock::now());

public:
	explicit log_data(string &&role, string &&cid, string &&rid) : role(role), cid(cid), rid(rid) {}
	virtual string get_log_str() = 0;
	[[nodiscard]] string get_log_time() const {
		return ctime(&log_time);
	}
	virtual ~log_data() = default;
};

class conn_log final : public log_data {
private:
	string state;

public:
	conn_log(string role, string cid, string rid, string state) : log_data(std::move(role), std::move(cid), std::move(rid)), state(std::move(state)) {}
	string get_log_str() override {
		string str = "\tRole : " + role + " / ID : ";
		if (role == "Client")
			str += cid + "\n\tSend-to : " + rid;
		else
			str += rid + "\n\tRecv-to : " + cid;
		str += "\n\tState : " + state;
		return str;
	}
};

class cmu_log final : public log_data {
private:
	string msg;

public:
	cmu_log(string role, string cid, string rid, string msg) : log_data(std::move(role), std::move(cid), std::move(rid)), msg(std::move(msg)) {}
	string get_log_str() override {
		string str = "\tRole : " + role + " / ID : ";
		if (role == "Client")
			str += cid + "\n\tSend-to : " + rid + "\n\tSend-Message : ";
		else
			str += rid + "\n\tRecv-to : " + cid + "\n\tRecv-Message : ";
		str += msg;
		return str;
	}
};

class log_handler {
private:
	ofstream logfile{"/ubuntu/log.txt", std::ios::app};
	// ofstream logfile {"log.txt", std::ios::app};
	vector<unique_ptr<log_data>> log_table;

public:
	void push_log(unique_ptr<log_data> data) {
		if (log_table.size() > 10000)
			log_table.erase(log_table.begin(), log_table.begin() + 1000);

		logfile << " :::: " << data->get_log_time() << data->get_log_str() << endl;
		log_table.push_back(std::move(data));
	}
	void prt_log() {
		if (log_table.empty()) {
			cout << "::::log table is empty::::" << endl;
			return;
		}
		int n{1};
		for (const auto &i : log_table) {
			cout << n++ << " :::: " << i->get_log_time() << i->get_log_str() << endl;
		}
	}
	~log_handler() {
		if (logfile.is_open()) {
			logfile.close();
		}
	}
};

/*
class conn_log : public log_data {
private:
	string state;
public:
	conn_log(string role,string cid,string rid, string state):
	log_data(std::move(role),std::move(cid),std::move(rid)),state(std::move(state)) {}
	void get_log_str(const int n){
		  cout<<n<<":\n\t"<<"Role:"<<role<<" / ID : ";
		  if(role == "Client") cout<<cid<<"\n\tSend-to : "<<rid;
		  else cout<<rid<<"\n\tRecv-to : "<<cid;
		  cout<<"\n\tState : "<<state<<endl;
		cout << "\tTime : " << std::ctime(&now_time);
	}
};

class cmu_log : public log_data{
private:
	string msg;
public:
	cmu_log(string role,string cid,string rid,string msg):
	log_data(std::move(role),std::move(cid),std::move(rid)),msg(std::move(msg)) {}
	void get_log_str(const int n){
		cout<<n<<":\n\t"<<"Role:"<<role<<" / ID : ";
		if(role == "Client") cout<<cid<<"\n\tSend-to : "<<rid<<"\n\tSend-Message : ";
		else cout<<rid<<"\n\tRecv-to : "<<cid<<"\n\tRecv-Message : ";
		cout<<msg<<endl;
		cout << "\tTime : " << std::ctime(&now_time);
	}
};
*/

#endif
