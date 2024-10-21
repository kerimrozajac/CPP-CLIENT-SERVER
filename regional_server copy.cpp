#include <iostream>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <sqlite3.h>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

using boost::asio::ip::tcp;

class RegionalServer {
public:
    RegionalServer(boost::asio::io_context& io_context, int regional_port, const std::string& db_file, const std::string& server_id, const std::string& central_server_ip, int central_port, int sync_interval)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), regional_port)), // Use the regional port argument
          central_server_endpoint_(tcp::endpoint(boost::asio::ip::address::from_string(central_server_ip), central_port)),
          socket_(io_context),
          db_file_(db_file),
          server_id_(server_id),
          sync_interval_(sync_interval),
          io_context_(io_context) {
        // Open the SQLite database
        if (sqlite3_open(db_file.c_str(), &db_) != SQLITE_OK) {
            std::cerr << "Failed to open database: " << sqlite3_errmsg(db_) << std::endl;
            return;
        }
        start_accept();
        start_sync();
    }

    ~RegionalServer() {
        sqlite3_close(db_);
    }

private:
    void start_accept() {
        acceptor_.async_accept(socket_, boost::bind(&RegionalServer::handle_accept, this, boost::asio::placeholders::error));
    }

    void handle_accept(const boost::system::error_code& error) {
        if (!error) {
            std::cout << "Client connected!" << std::endl;
            // Handle client interaction if needed
        } else {
            std::cerr << "Error on accept: " << error.message() << std::endl;
        }
        start_accept();  // Ready to accept the next connection
    }

    void start_sync() {
        std::thread([this]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::minutes(sync_interval_));
                synchronize_with_central();
            }
        }).detach();
    }

    void synchronize_with_central() {
        try {
            boost::asio::io_context sync_context;
            tcp::socket sync_socket(sync_context);
            sync_socket.connect(central_server_endpoint_);

            // Prepare the database data to be sent
            std::string db_data = get_database_data();
            std::string sync_message = "SYNC|" + server_id_ + "|" + db_data;

            // Send the synchronization message
            boost::asio::write(sync_socket, boost::asio::buffer(sync_message));

            std::cout << "Synchronization data sent to central server." << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to sync with central server: " << e.what() << std::endl;
        }
    }

    std::string get_database_data() {
        std::string db_data;

        // Retrieve data from the database tables
        db_data += get_table_data("korisnici");
        db_data += get_table_data("usluge");
        db_data += get_table_data("narudzbe");
        db_data += get_table_data("loyalnost");

        return db_data;
    }

    std::string get_table_data(const std::string& table_name) {
        std::string table_data;
        sqlite3_stmt* stmt;
        std::string query = "SELECT * FROM " + table_name;  // Query to select all from the table
        if (sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                for (int col = 0; col < sqlite3_column_count(stmt); ++col) {
                    if (col > 0) {
                        table_data += ","; // Add delimiter for columns
                    }
                    if (sqlite3_column_type(stmt, col) == SQLITE_INTEGER) {
                        table_data += std::to_string(sqlite3_column_int(stmt, col));
                    } else if (sqlite3_column_type(stmt, col) == SQLITE_TEXT) {
                        table_data += std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, col)));
                    }
                }
                table_data += "\n"; // Newline for each row
            }
        }
        sqlite3_finalize(stmt);
        return table_data;
    }

    tcp::acceptor acceptor_;
    tcp::endpoint central_server_endpoint_;
    tcp::socket socket_;
    sqlite3* db_;
    std::string db_file_;
    std::string server_id_;
    int sync_interval_;
    boost::asio::io_context& io_context_;
};

int main(int argc, char* argv[]) {
    if (argc != 7) {
        std::cerr << "Usage: ./regional_server <regional_port> <central_server_ip> <central_server_port> <server_id> <database> <sync_interval_minutes>" << std::endl;
        return 1;
    }

    int regional_port = std::stoi(argv[1]);
    std::string central_server_ip = argv[2];
    int central_port = std::stoi(argv[3]);
    std::string server_id = argv[4];
    std::string db_file = argv[5];
    int sync_interval = std::stoi(argv[6]);

    try {
        boost::asio::io_context io_context;
        RegionalServer server(io_context, regional_port, db_file, server_id, central_server_ip, central_port, sync_interval); // Pass the regional_port
        io_context.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
