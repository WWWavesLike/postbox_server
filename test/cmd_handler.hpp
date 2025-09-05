#ifndef CMDHANDLER_HPP
#define CMDHANDLER_HPP
#include <fstream>
#include <iostream>
#include <string>
#include "../communication/communication.hpp"

using namespace std;

class cmd_handler {
private:
    string input_line;
    communication& cmu;
public:
    cmd_handler(communication& cmu) : cmu {cmu}{}
    void running(){
        while(true) {
            std::cout << "Enter command :::: ";
            std::getline(std::cin, input_line);  // 사용자로부터 한 줄 입력 받기

            if (input_line == "rtable") {
                cmu.show_recv_table();
                continue;
            }
            if (input_line == "ctable") {
                cmu.show_client_table();
                continue;
            }
            if (input_line == "log") {
                cmu.log_hdr.prt_log();
                continue;
            }
            else {
                std::cout << "::::Wrong command::::\n";
            }
        }
    }
};

#endif