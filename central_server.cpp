#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <sqlite3.h>
#include <memory>
#include <vector>
#include <sstream>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp> // For version string
#include <boost/asio/ip/tcp.hpp>   // For TCP functionality

namespace beast = boost::beast; // For convenience
namespace http = beast::http;    // For HTTP types
using tcp = boost::asio::ip::tcp; // For TCP

// Function declarations
void insert_korisnici(sqlite3* db, const std::string& server_id, const std::string& data);
void insert_usluge(sqlite3* db, const std::string& server_id, const std::string& data);
void insert_narudzbe(sqlite3* db, const std::string& server_id, const std::string& data);
void insert_loyalnost(sqlite3* db, const std::string& server_id, const std::string& data);
void insert_data(sqlite3* db, const std::string& sql, const std::string& server_id, const std::string& data);

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::io_context& io_context, sqlite3* db)
        : socket_(io_context), db_(db) {}

    tcp::socket& socket() {
        return socket_;
    }

    void start() {
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_, buffer_, '\n',
            boost::bind(&Session::handle_read, self, boost::asio::placeholders::error));
    }

private:
    void handle_read(const boost::system::error_code& error) {
        if (!error) {
            std::istream is(&buffer_);
            std::string message;
            std::getline(is, message);

            std::cout << "Received data: " << message << std::endl;

            // Split the message into server_id and data
            std::size_t separator_pos = message.find('|');
            if (separator_pos != std::string::npos) {
                std::string server_id = message.substr(0, separator_pos);
                std::string data = message.substr(separator_pos + 1);

                // Process the received data for different tables
                process_data(server_id, data);
            }

            auto self(shared_from_this());
            boost::asio::async_read_until(socket_, buffer_, '\n',
                boost::bind(&Session::handle_read, self, boost::asio::placeholders::error));
        }
    }

    void process_data(const std::string& server_id, const std::string& data) {
        std::istringstream data_stream(data);
        std::string row;
        while (std::getline(data_stream, row)) {
            std::size_t table_sep_pos = row.find(",");
            if (table_sep_pos != std::string::npos) {
                std::string table_type = row.substr(0, table_sep_pos);

                if (table_type == "korisnici") {
                    insert_korisnici(db_, server_id, row.substr(table_sep_pos + 1));
                } else if (table_type == "usluge") {
                    insert_usluge(db_, server_id, row.substr(table_sep_pos + 1));
                } else if (table_type == "narudzbe") {
                    insert_narudzbe(db_, server_id, row.substr(table_sep_pos + 1));
                } else if (table_type == "loyalnost") {
                    insert_loyalnost(db_, server_id, row.substr(table_sep_pos + 1));
                }
            }
        }
    }

    tcp::socket socket_;
    boost::asio::streambuf buffer_;
    sqlite3* db_;
};

class Server {
public:
    Server(boost::asio::io_context& io_context, short start_port, short end_port, sqlite3* db)
        : io_context_(io_context), db_(db) {
        start_accept(start_port, end_port);
    }

private:
    void start_accept(short start_port, short end_port) {
        for (short port = start_port; port <= end_port; ++port) {
            auto new_session = std::make_shared<Session>(io_context_, db_);
            acceptor_.emplace_back(io_context_, tcp::endpoint(tcp::v4(), port));
            acceptor_.back().async_accept(new_session->socket(),
                boost::bind(&Server::handle_accept, this, new_session,
                boost::asio::placeholders::error));
        }
    }

    void handle_accept(std::shared_ptr<Session> new_session, const boost::system::error_code& error) {
        if (!error) {
            new_session->start();
        }
    }

    std::vector<tcp::acceptor> acceptor_; // Adjusted for multiple acceptors
    boost::asio::io_context& io_context_;
    sqlite3* db_;
};

void create_database(sqlite3* db) {
    const char* create_table_sql =
        "CREATE TABLE IF NOT EXISTS sync_data ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "server_id TEXT NOT NULL,"
        "data TEXT NOT NULL,"
        "timestamp TEXT NOT NULL);";

    char* err_msg = nullptr;
    if (sqlite3_exec(db, create_table_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "Error creating table: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
}

void insert_data(sqlite3* db, const std::string& sql, const std::string& server_id, const std::string& data) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, server_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, data.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Error inserting data: " << sqlite3_errmsg(db) << std::endl;
        }
    }
    sqlite3_finalize(stmt);
}

void insert_korisnici(sqlite3* db, const std::string& server_id, const std::string& data) {
    insert_data(db, "INSERT INTO sync_data (server_id, data, timestamp) VALUES (?, ?, datetime('now'));", server_id, data);
}

void insert_usluge(sqlite3* db, const std::string& server_id, const std::string& data) {
    insert_data(db, "INSERT INTO sync_data (server_id, data, timestamp) VALUES (?, ?, datetime('now'));", server_id, data);
}

void insert_narudzbe(sqlite3* db, const std::string& server_id, const std::string& data) {
    insert_data(db, "INSERT INTO sync_data (server_id, data, timestamp) VALUES (?, ?, datetime('now'));", server_id, data);
}

void insert_loyalnost(sqlite3* db, const std::string& server_id, const std::string& data) {
    insert_data(db, "INSERT INTO sync_data (server_id, data, timestamp) VALUES (?, ?, datetime('now'));", server_id, data);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: ./central_server <start_port> <end_port> <database_file>" << std::endl;
        return 1;
    }

    short start_port = std::atoi(argv[1]);
    short end_port = std::atoi(argv[2]);
    const std::string database_file = argv[3];

    sqlite3* db;
    if (sqlite3_open(database_file.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }

    create_database(db); // Create the database table

    boost::asio::io_context io_context;
    Server server(io_context, start_port, end_port, db); // Use the specified port range
    io_context.run();

    sqlite3_close(db);
    return 0;
}
