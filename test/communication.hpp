#ifndef COMMUNICATION_H
#define COMMUNICATION_H
#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include "map_handler.hpp"
#include "log_handler.hpp"
#include <chrono>

/*
    등록 msg : "id"&"send_to"$"C" or "R"
    C 이면 클라이언트
    R 이면 리시버
*/

using boost::asio::ip::tcp;

/*
ostream& operator<<(ostream& os,tcp::socket& sock) {
    os<<sock.native_handle();
    return os;
}
*/


struct acc_info {
    string id;
    string send_to;
    char role;
};

class communication : public map_handler{
public:
    log_handler log_hdr;
    void msg_unpacking(const char* buf, acc_info& info) {
        string msg = buf;
        const size_t id_pos = msg.find('&');
        const size_t role_pos = msg.find('$');
        if ((id_pos != string::npos && role_pos != string::npos)) [[likely]]{
            info.id = msg.substr(0, id_pos);
            info.send_to = msg.substr(id_pos+1,role_pos - id_pos - 1);
            if (msg.substr(role_pos+1)=="C") info.role = 'C';
            else if(msg.substr(role_pos+1)=="R") info.role = 'R';
            else info.role = 'E';
        } else {
            info.role = 'E';
        }
    }

    /*
     리시버에 종료 기능을 넣지 않아 소켓 확인이 불가능함.
     핑을 통해 접속을 확인함. 추후 리시버에 소켓 종료 기능을 넣을 것.
     */
    void recv_th(tcp::socket& sock, boost::system::error_code& error, const acc_info& info,boost::array<char, 1024>& buf) {
        if(secure_ins(info.send_to, info.id)) return;
        sock.write_some(boost::asio::buffer("OK"), error);
        if (error == boost::asio::error::eof) {
            return;
        } else if (error) {
            return;
        }
        string msg;
        log_data* data;
        while(true) {
            if(is_empty(info.send_to)){
                msg = pop(info.send_to);
                sock.write_some(boost::asio::buffer(msg), error);
                if (error) {
                    break;
                }
                fill(buf.begin(), buf.end(), 0);
                data = new(nothrow) cmu_log("Recv",info.id,info.send_to,msg);
                if(data != nullptr) {
                    log_hdr.push_log(unique_ptr<log_data>(data));
                    data = nullptr;
                }
            }
            else {
                sock.write_some(boost::asio::buffer(""), error);
                if (error == boost::asio::error::eof) {

                    break;
                } else if (error) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
        erase(info.send_to);
    }
    void client_th(tcp::socket& sock, boost::system::error_code& error,acc_info& info,boost::array<char, 1024>& buf) {
        if(secure_client_check(info.id)) return;
        sock.write_some(boost::asio::buffer("OK"), error);
        if (error == boost::asio::error::eof) {
            return;
        } else if (error) {
            return;
        }
        log_data* data;
        while(sock.is_open()) {
            sock.read_some(boost::asio::buffer(buf), error);
            if (error == boost::asio::error::eof) {
                break;
            } else if (error) {
                break;
            }
            if(is_cntn(info.send_to)) {
                if(secure_check(info.send_to, info.id)) return;
                if(append_q(info.send_to,buf.data())){
                    sock.write_some(boost::asio::buffer("queue is full.\n"), error);
                }
                else {
                    data = new(nothrow) cmu_log("Client",info.id,info.send_to,buf.data());
                    if(data != nullptr) {
                        log_hdr.push_log(unique_ptr<log_data>(data));
                        data = nullptr;
                    }
                    sock.write_some(boost::asio::buffer("OK.\n"), error);
                }
            }
            else{sock.write_some(boost::asio::buffer("recvier is not connecting.\n"), error);}
            fill(buf.begin(), buf.end(), 0);
        }
        erase_ctable(info.id);
    }

    void cmi_start(tcp::socket&& sock) {
        boost::array<char, 1024> buf;
        boost::system::error_code error;
        //string id, send_to;
        acc_info info;
        sock.read_some(boost::asio::buffer(buf), error);
        if(error) return;
        msg_unpacking(buf.data(),info);
        log_data* data;
        fill(buf.begin(), buf.end(), 0);
        switch (info.role) {
            case 'R' :
                data = new(nothrow) conn_log("Recv",info.id,info.send_to,"Conn");
                if(data != nullptr) {
                    log_hdr.push_log(unique_ptr<log_data>(data));
                    data = nullptr;
                }
                recv_th(sock, error, info, buf);
                data = new(nothrow) conn_log("Recv",info.id,info.send_to,"Close");
                if(data != nullptr) {
                    log_hdr.push_log(unique_ptr<log_data>(data));
                    data = nullptr;
                }
                break;
            case 'C' :
                data = new(nothrow) conn_log("Client",info.id,info.send_to,"Conn");
                if(data != nullptr) {
                    log_hdr.push_log(unique_ptr<log_data>(data));
                    data = nullptr;
                }
                client_th(sock, error, info,buf);
                data = new(nothrow) conn_log("Client",info.id,info.send_to,"Close");
                if(data != nullptr) {
                    log_hdr.push_log(unique_ptr<log_data>(data));
                    data = nullptr;
                }
                break;
            default :
                sock.write_some(boost::asio::buffer("ER"), error);
        }
        sock.close();
    }
};

#endif //COMMUNICATION_H




/*
        switch (info.role) {
            case 'R' :
                log_hdr.push_log(make_unique<conn_log>("Recv",info.id,info.send_to,"Conn"));
                recv_th(sock, error, info, buf);
                log_hdr.push_log(make_unique<conn_log>("Recv",info.id,info.send_to,"Close"));
                break;
            case 'C' :
                log_hdr.push_log(make_unique<conn_log>("Client",info.id,info.send_to,"Conn"));
                client_th(sock, error, info,buf);
                log_hdr.push_log(make_unique<conn_log>("Client",info.id,info.send_to,"Close"));
                break;
            default :
                sock.write_some(boost::asio::buffer("ER"), error);
        }
 */