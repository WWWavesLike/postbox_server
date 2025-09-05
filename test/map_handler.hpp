#ifndef MAP_HANDLER_H
#define MAP_HANDLER_H
#include <unordered_map>
#include <string>
#include <boost/asio.hpp>
#include <iostream>
#include <mutex>
#include <queue>
#include <vector>
#include <utility>

using boost::asio::ip::tcp;
using namespace std;

ostream& operator<<(ostream& os,tcp::socket* sock) {
    os<<sock->native_handle();
    return os;
}

class map_handler {
private:
    unordered_map<string, string> secure_table;
    unordered_map<string, queue<string>> rid_table;
    vector<string> cid_table;
    mutex mtx;
public:
    bool is_cntn(const string& id) const{
        return rid_table.contains(id);
    }
    bool is_empty(const string& id) {
        return !rid_table[id].empty();
    }
    string pop(const string& id) {
        string rtn = rid_table[id].front();
        rid_table[id].pop();
        return rtn;
    }
    bool secure_client_check(string& cid) {
        bool res = false;
        {
            lock_guard<std::mutex> lock(mtx);
            for(auto& it : cid_table) {
                if(it == cid) res = true;
            }
            if(!res)cid_table.push_back(cid);
        }
        return res;
    }
    bool secure_ins(const string& rid, const string& cid) {
        bool res = true;
        {
            lock_guard<std::mutex> lock(mtx);
            if(!secure_table.contains(rid)){
                secure_table[rid]=cid;
                res = false;
            }
        }
        return res;
    }

    bool secure_check(const string& rid, const string& cid) {
        if(secure_table.contains(rid)) {
            if(secure_table[rid]==cid) return false;
        }
        return true;
    }
    bool append_q(string& id, string&& cmd){
        if(rid_table[id].size()>=256) {
            return true;
        }
        rid_table[id].emplace(move(cmd));
        return false;
    }

    bool insert_id(const string& id) {
        bool res = true;
        {
            std::lock_guard<std::mutex> lock(mtx);
            if(!rid_table.contains(id)) {
                string nid = id;
                rid_table[nid];
                res=false;
            }
        }
        return res;
    }

    void erase(const string& send_to) {
        secure_table.erase(send_to);
        rid_table.erase(send_to);
    }
    void erase_ctable(const string& cid) {
        cid_table.erase(remove(cid_table.begin(), cid_table.end(), cid),cid_table.end());
    }
    void show_client_table() const {
        if(cid_table.empty()) cout<<"::::client_table is empty::::\n";
        else {
            cout << "client_sockets ::::\n";
            int n = 1;
            for (auto& it : cid_table) {
                cout << n++ <<". Key: " << it << '\n';
            }
        }
    }
    void show_recv_table() const {
        if(rid_table.empty()) cout<<"::::recv_table is empty::::\n";
        else {
            cout << "recv_sockets ::::\n";
            int n = 1;
            for (auto it = rid_table.cbegin(); it != rid_table.cend(); ++it) {
                cout << n++ <<". Key: " << it->first << '\n';
            }
        }
    }
};

#endif //MAP_HANDLER_H
