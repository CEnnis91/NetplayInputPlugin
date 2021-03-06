#include "stdafx.h"

#include "server.h"
#include "room.h"
#include "user.h"
#include "version.h"

using namespace std;
using namespace asio;

server::server(shared_ptr<io_service> io_s, bool multiroom) : io_s(io_s), multiroom(multiroom), acceptor(*io_s), timer(*io_s) { }

uint16_t server::open(uint16_t port) {
    error_code error;

    auto ipv = ip::tcp::v6();
    acceptor.open(ipv, error);
    if (error) { // IPv6 not available
        ipv = ip::tcp::v4();
        acceptor.open(ipv, error);
        if (error) throw error;
    }

    acceptor.bind(ip::tcp::endpoint(ipv, port), error);
    if (error) throw error;

    acceptor.listen(MAX_PLAYERS, error);
    if (error) throw error;

    timer.expires_from_now(std::chrono::seconds(1));
    timer.async_wait([=](const error_code& error) { if (!error) on_tick(); });

    accept();

    return acceptor.local_endpoint().port();
}

void server::close() {
    error_code error;

    if (acceptor.is_open()) {
        acceptor.close(error);
    }

    timer.cancel(error);

    for (auto& e : rooms) {
        e.second->close();
    }
}

void server::accept() {
    auto u = make_shared<user>(io_s, shared_from_this());
    acceptor.async_accept(u->get_socket(), [=](error_code error) {
        if (error) return;
        
        u->get_socket().set_option(ip::tcp::no_delay(true), error);
        if (error) return;

        u->send_protocol_version();
        u->process_packet();

        accept();
    });
}

void server::on_user_join(user_ptr user, string room_id) {
    if (multiroom) {
        if (room_id == "") room_id = get_random_room_id();
    } else {
        room_id = "";
    }

    if (rooms.find(room_id) == rooms.end()) {
        rooms[room_id] = make_shared<room>(room_id, shared_from_this());
        cout << "Room " << room_id << " created. Room count: " << rooms.size() << endl;
    }
    rooms[room_id]->on_user_join(user);
}

void server::on_room_close(room_ptr room) {
    if (rooms.erase(room->get_id())) {
        cout << "Room " << room->get_id() << " destroyed. Room count: " << rooms.size() << endl;
    }
}

void server::on_tick() {
    for (auto& e : rooms) {
        e.second->on_tick();
    }

    timer.expires_from_now(std::chrono::seconds(1));
    timer.async_wait([=](const error_code& error) { if (!error) on_tick(); });
}

string server::get_random_room_id() {
    static const string base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    static uniform_int_distribution<size_t> dist(0, base64.length() - 1);
    static random_device rd;

    string result;
    result.resize(4);
    do {
        for (char& c : result) {
            c = base64[dist(rd)];
        }
    } while (rooms.find(result) != rooms.end());

    return result;
}

int main(int argc, char* argv[]) {
    cout << APP_NAME_AND_VERSION << endl;
    try {
        uint16_t port = argc >= 2 ? stoi(argv[1]) : 6400;
        auto io_s = make_shared<io_service>();
        auto my_server = make_shared<server>(io_s, true);
        port = my_server->open(port);
        cout << "Listening on port " << port << "..." << endl;
        io_s->run();
    } catch (const exception& e) {
        cerr << e.what() << endl;
        return 1;
    } catch (const error_code& e) {
        cerr << e.message() << endl;
        return 1;
    }

    return 0;
}
