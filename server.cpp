#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json/src.hpp>
#include <sqlite3.h>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <random>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

namespace json = boost::json;

// SQLite database handler
sqlite3* db;

// Generate a random string as a token
std::string generate_token(size_t length) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);

    std::string token;
    for (size_t i = 0; i < length; ++i) {
        token += alphanum[dis(gen)];
    }
    return token;
}

bool is_token_valid(const std::string& token) {
    std::string sql = "SELECT user_id FROM Sessions WHERE auth_token = ?";
    sqlite3_stmt* stmt;
    
    // Prepare the SQL statement
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing token validation query: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    // Bind the token to the query
    sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);

    // Execute the query and check if a row was returned
    bool valid = sqlite3_step(stmt) == SQLITE_ROW;

    // Finalize the statement
    sqlite3_finalize(stmt);


    return valid;
}


void handle_get_profile(const std::string& token, http::response<http::string_body>& res) {
    if (is_token_valid(token)) {
        std::string sql = "SELECT username, email FROM Korisnici WHERE user_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                json::object response_body;
                response_body["username"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                response_body["email"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                
                std::string response_str = json::serialize(response_body);
                res.result(http::status::ok);
                res.body() = response_str;
            } else {
                res.result(http::status::not_found);
                res.body() = "Profile not found";
            }
            sqlite3_finalize(stmt);
        } else {
            res.result(http::status::internal_server_error);
            res.body() = "Database query error: " + std::string(sqlite3_errmsg(db));
        }
    } else {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
    }
}




// Handle login request
void handle_login(const std::string& body, http::response<http::string_body>& res) {
    std::string username, password;

    // Extract username and password from the request body
    auto pos = body.find("username=");
    if (pos != std::string::npos) {
        auto end_pos = body.find('&', pos);
        username = body.substr(pos + 9, end_pos == std::string::npos ? body.size() - pos - 9 : end_pos - pos - 9);
    }
    pos = body.find("password=");
    if (pos != std::string::npos) {
        password = body.substr(pos + 9);
    }

    // SQL query to fetch user ID and type
    std::string sql = "SELECT user_id, user_type FROM Korisnici WHERE username = ? AND password = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int user_id = sqlite3_column_int(stmt, 0);
            std::string user_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            std::string token = generate_token(32);

            // Insert the new session into the Sessions table
            std::string insert_sql = "INSERT INTO Sessions (user_id, auth_token) VALUES (?, ?)";
            sqlite3_stmt* insert_stmt;
            if (sqlite3_prepare_v2(db, insert_sql.c_str(), -1, &insert_stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(insert_stmt, 1, user_id);
                sqlite3_bind_text(insert_stmt, 2, token.c_str(), -1, SQLITE_STATIC);

                if (sqlite3_step(insert_stmt) == SQLITE_DONE) {
                    // Create JSON response
                    json::object response_body;
                    response_body["success"] = true;
                    response_body["token"] = token;
                    response_body["user_type"] = user_type;

                    std::string response_str = json::serialize(response_body);
                    res.result(http::status::ok);
                    res.body() = response_str;
                } else {
                    res.result(http::status::internal_server_error);
                    res.body() = "Error creating session: " + std::string(sqlite3_errmsg(db));
                }
                sqlite3_finalize(insert_stmt);
            } else {
                res.result(http::status::internal_server_error);
                res.body() = "Error preparing session insert: " + std::string(sqlite3_errmsg(db));
            }
        } else {
            res.result(http::status::unauthorized);
            res.body() = "Invalid username or password";
        }
        sqlite3_finalize(stmt);
    } else {
        res.result(http::status::internal_server_error);
        res.body() = "Database query error: " + std::string(sqlite3_errmsg(db));
    }
}


// URL-decode a string
std::string url_decode(const std::string& str) {
    std::string decoded;
    decoded.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%') {
            if (i + 2 < str.size()) {
                int value = std::stoi(str.substr(i + 1, 2), nullptr, 16);
                decoded += static_cast<char>(value);
                i += 2;
            }
        } else if (str[i] == '+') {
            decoded += ' ';
        } else {
            decoded += str[i];
        }
    }

    return decoded;
}

// Handle registration request
void handle_register(const std::string& body, http::response<http::string_body>& res) {
    std::string username, email, password;

    auto get_field_value = [&](const std::string& field_name) {
        auto pos = body.find(field_name + "=");
        if (pos != std::string::npos) {
            auto end_pos = body.find('&', pos);
            return url_decode(body.substr(pos + field_name.size() + 1, end_pos == std::string::npos ? body.size() - pos - field_name.size() - 1 : end_pos - pos - field_name.size() - 1));
        }
        return std::string{};
    };

    username = get_field_value("username");
    email = get_field_value("email");
    password = get_field_value("password");

    std::string sql = "INSERT INTO Korisnici (username, email, password, user_type) VALUES (?, ?, ?, 'buyer')";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, password.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            res.result(http::status::ok);
            res.body() = "Registration successful";
        } else {
            res.result(http::status::internal_server_error);
            res.body() = "Error registering user: " + std::string(sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    } else {
        res.result(http::status::internal_server_error);
        res.body() = "Database query error: " + std::string(sqlite3_errmsg(db));
    }
}

// Handle profile request
void handle_profile(const std::string& token, http::response<http::string_body>& res) {
    if (is_token_valid(token)) {
        res.result(http::status::ok);
        res.body() = "Profile access granted";
    } else {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
    }
}

// Handle update profile request
void handle_update_profile(const std::string& token, const std::string& body, http::response<http::string_body>& res) {
    if (is_token_valid(token)) {
        // Extract fields from body to update
        std::string username, password, email, profile_type;

        auto get_field_value = [&](const std::string& field_name) {
            auto pos = body.find(field_name + "=");
            if (pos != std::string::npos) {
                auto end_pos = body.find('&', pos);
                return url_decode(body.substr(pos + field_name.size() + 1, end_pos == std::string::npos ? body.size() - pos - field_name.size() - 1 : end_pos - pos - field_name.size() - 1));
            }
            return std::string{};
        };

        username = get_field_value("username");
        password = get_field_value("password");
        email = get_field_value("email");
        profile_type = get_field_value("profile_type");

        // Update profile in the database
        std::string sql = "UPDATE Korisnici SET username = ?, password = ?, email = ?, profile_type = ? WHERE user_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, email.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, profile_type.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 5, token.c_str(), -1, SQLITE_STATIC);

            if (sqlite3_step(stmt) == SQLITE_DONE) {
                res.result(http::status::ok);
                res.body() = "Profile updated successfully";
            } else {
                res.result(http::status::internal_server_error);
                res.body() = "Error updating profile: " + std::string(sqlite3_errmsg(db));
            }
            sqlite3_finalize(stmt);
        } else {
            res.result(http::status::internal_server_error);
            res.body() = "Error preparing update profile query: " + std::string(sqlite3_errmsg(db));
        }
    } else {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
    }
}




// Handle logout request
void handle_logout(const std::string& token, http::response<http::string_body>& res) {
    if (is_token_valid(token)) {
        // Delete the session from the Sessions table
        std::string delete_sql = "DELETE FROM Sessions WHERE auth_token = ?";
        sqlite3_stmt* delete_stmt;
        if (sqlite3_prepare_v2(db, delete_sql.c_str(), -1, &delete_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(delete_stmt, 1, token.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(delete_stmt) == SQLITE_DONE) {
                res.result(http::status::ok);
                res.body() = "Logged out successfully";
            } else {
                res.result(http::status::internal_server_error);
                res.body() = "Error logging out: " + std::string(sqlite3_errmsg(db));
            }
            sqlite3_finalize(delete_stmt);
        } else {
            res.result(http::status::internal_server_error);
            res.body() = "Error preparing logout: " + std::string(sqlite3_errmsg(db));
        }
    } else {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
    }
}

// Function to handle services menu
void handle_services(const std::string& token, const std::string& body, http::response<http::string_body>& res) {
    std::cout << "Token: " << token << std::endl;
    std::cout << "Request Body: " << body << std::endl;

    if (!is_token_valid(token)) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    std::string user_type;
    std::string sql = "SELECT user_type FROM Korisnici WHERE user_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            user_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    } else {
        res.result(http::status::internal_server_error);
        res.body() = "Error retrieving user type: " + std::string(sqlite3_errmsg(db));
        return;
    }

    if (user_type == "seller") {
        res.result(http::status::ok);
        res.body() = "Services Menu:\n1. Create Service\n2. View My Services\n3. Delete Service\n4. Update Service";
    } else if (user_type == "buyer") {
        res.result(http::status::ok);
        res.body() = "All Services:\n";
        
        sql = "SELECT service_id, service_name, price, capacity, working_hours, service_type, loyalty_requirement, loyalty_discount FROM Usluge";
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                res.body() += "ID: " + std::to_string(sqlite3_column_int(stmt, 0)) + ", "
                              "Service Name: " + reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)) + ", "
                              "Price: " + std::to_string(sqlite3_column_double(stmt, 2)) + ", "
                              "Capacity: " + std::to_string(sqlite3_column_int(stmt, 3)) + ", "
                              "Hours: " + reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)) + ", "
                              "Type: " + reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)) + ", "
                              "Loyalty Req: " + reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)) + ", "
                              "Loyalty Discount: " + std::to_string(sqlite3_column_double(stmt, 7)) + "\n";
            }
            sqlite3_finalize(stmt);
        } else {
            res.result(http::status::internal_server_error);
            res.body() = "Error retrieving services: " + std::string(sqlite3_errmsg(db));
        }
    } else {
        res.result(http::status::forbidden);
        res.body() = "Unknown user type";
    }
}

void handle_create_service(const std::string& token, const std::string& body, http::response<http::string_body>& res) {
    if (!is_token_valid(token)) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    std::string service_name, price, capacity, working_hours, service_type, loyalty_requirement, loyalty_discount;
    std::istringstream stream(body);
    std::string segment;
    while (std::getline(stream, segment, '&')) {
        auto delimiter_pos = segment.find('=');
        std::string key = segment.substr(0, delimiter_pos);
        std::string value = segment.substr(delimiter_pos + 1);
        
        if (key == "service_name") service_name = value;
        else if (key == "price") price = value;
        else if (key == "capacity") capacity = value;
        else if (key == "working_hours") working_hours = value;
        else if (key == "service_type") service_type = value;
        else if (key == "loyalty_requirement") loyalty_requirement = value;
        else if (key == "loyalty_discount") loyalty_discount = value;
    }

    std::string sql = "INSERT INTO Usluge (service_name, price, capacity, working_hours, service_type, loyalty_requirement, loyalty_discount, seller_id) VALUES (?, ?, ?, ?, ?, ?, ?, (SELECT user_id FROM Sessions WHERE auth_token = ?))";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, service_name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 2, std::stod(price));
        sqlite3_bind_int(stmt, 3, std::stoi(capacity));
        sqlite3_bind_text(stmt, 4, working_hours.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, service_type.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, loyalty_requirement.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 7, std::stod(loyalty_discount));
        sqlite3_bind_text(stmt, 8, token.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            res.result(http::status::ok);
            res.body() = "Service created successfully";
        } else {
            res.result(http::status::internal_server_error);
            res.body() = "Error creating service: " + std::string(sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    } else {
        res.result(http::status::internal_server_error);
        res.body() = "Error preparing SQL statement: " + std::string(sqlite3_errmsg(db));
    }
}

void handle_delete_service(const std::string& token, const std::string& service_id_str, http::response<http::string_body>& res) {
    if (!is_token_valid(token)) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    int service_id = std::stoi(service_id_str);
    std::string sql = "DELETE FROM Usluge WHERE service_id = ? AND seller_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, service_id);
        sqlite3_bind_text(stmt, 2, token.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            res.result(http::status::ok);
            res.body() = "Service deleted successfully";
        } else {
            res.result(http::status::internal_server_error);
            res.body() = "Error deleting service: " + std::string(sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    } else {
        res.result(http::status::internal_server_error);
        res.body() = "Error preparing SQL statement: " + std::string(sqlite3_errmsg(db));
    }
}

void handle_update_service(const std::string& token, const std::string& service_id_str, const std::string& field_and_value, http::response<http::string_body>& res) {
    if (!is_token_valid(token)) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    int service_id = std::stoi(service_id_str);
    auto delimiter_pos = field_and_value.find('=');
    std::string field_name = field_and_value.substr(0, delimiter_pos);
    std::string new_value = field_and_value.substr(delimiter_pos + 1);

    std::string sql = "UPDATE Usluge SET " + field_name + " = ? WHERE service_id = ? AND seller_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, new_value.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, service_id);
        sqlite3_bind_text(stmt, 3, token.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            res.result(http::status::ok);
            res.body() = "Service updated successfully";
        } else {
            res.result(http::status::internal_server_error);
            res.body() = "Error updating service: " + std::string(sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    } else {
        res.result(http::status::internal_server_error);
        res.body() = "Error preparing SQL statement: " + std::string(sqlite3_errmsg(db));
    }
}


// Function to handle the "my services" request
void handle_my_services(const std::string& token, http::response<http::string_body>& res) {
    // Validate the token
    if (!is_token_valid(token)) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    // Query to get the seller's services
    std::string sql = "SELECT service_id, service_name, price, capacity, working_hours, service_type, loyalty_requirement, loyalty_discount "
                      "FROM Usluge WHERE seller_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
    sqlite3_stmt* stmt;
    
    // Prepare and execute the SQL query
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);

        std::string services_list;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            services_list += "ID: " + std::to_string(sqlite3_column_int(stmt, 0)) + ", "
                            "Service Name: " + reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)) + ", "
                            "Price: " + std::to_string(sqlite3_column_double(stmt, 2)) + ", "
                            "Capacity: " + std::to_string(sqlite3_column_int(stmt, 3)) + ", "
                            "Hours: " + reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)) + ", "
                            "Type: " + reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)) + ", "
                            "Loyalty Req: " + reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)) + ", "
                            "Loyalty Discount: " + std::to_string(sqlite3_column_double(stmt, 7)) + "\n";
        }
        sqlite3_finalize(stmt);

        if (!services_list.empty()) {
            res.result(http::status::ok);
            res.body() = "My Services:\n" + services_list;
        } else {
            res.result(http::status::ok);
            res.body() = "No services found.";
        }
    } else {
        res.result(http::status::internal_server_error);
        res.body() = "Error retrieving services: " + std::string(sqlite3_errmsg(db));
    }
}



void handle_orders(const std::string& body, const std::string& token, http::response<http::string_body>& res) {
    if (!is_token_valid(token)) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    std::string user_type;
    std::string sql = "SELECT user_type FROM Korisnici WHERE user_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            user_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    } else {
        res.result(http::status::internal_server_error);
        res.body() = "Error retrieving user type: " + std::string(sqlite3_errmsg(db));
        return;
    }

    if (user_type == "buyer") {
        if (body.find("view=true") != std::string::npos) {
            // View My Orders
            sql = "SELECT order_id, service_id, amount, status FROM Orders WHERE buyer_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                std::string orders_list;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    orders_list += "Order ID: " + std::to_string(sqlite3_column_int(stmt, 0)) + ", "
                                    "Service ID: " + std::to_string(sqlite3_column_int(stmt, 1)) + ", "
                                    "Amount: " + std::to_string(sqlite3_column_int(stmt, 2)) + ", "
                                    "Status: " + reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)) + "\n";
                }
                sqlite3_finalize(stmt);

                if (!orders_list.empty()) {
                    res.result(http::status::ok);
                    res.body() = "My Orders:\n" + orders_list;
                } else {
                    res.result(http::status::ok);
                    res.body() = "No orders found.";
                }
            } else {
                res.result(http::status::internal_server_error);
                res.body() = "Error retrieving orders: " + std::string(sqlite3_errmsg(db));
            }
        } else if (body.find("make") != std::string::npos) {
            std::string service_id_str, amount_str;
            auto get_field_value = [&](const std::string& field_name) {
                auto pos = body.find(field_name + "=");
                if (pos != std::string::npos) {
                    auto end_pos = body.find('&', pos);
                    return url_decode(body.substr(pos + field_name.size() + 1, end_pos == std::string::npos ? body.size() - pos - field_name.size() - 1 : end_pos - pos - field_name.size() - 1));
                }
                return std::string{};
            };

            service_id_str = get_field_value("service_id");
            amount_str = get_field_value("amount");

            int service_id = std::stoi(service_id_str);
            int amount = std::stoi(amount_str);

            // Check service capacity and get price
            sql = "SELECT capacity, price, loyalty_discount, loyalty_requirement FROM Usluge WHERE service_id = ?";
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, service_id);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    int capacity = sqlite3_column_int(stmt, 0);
                    double price = sqlite3_column_double(stmt, 1);
                    double loyalty_discount = sqlite3_column_double(stmt, 2);
                    int loyalty_requirement = sqlite3_column_int(stmt, 3);

                    if (amount > capacity) {
                        res.result(http::status::bad_request);
                        res.body() = "Requested amount exceeds service capacity";
                        sqlite3_finalize(stmt);
                        return;
                    }

                    // Check buyer's loyalty points
                    sql = "SELECT loyalty_points FROM Korisnici WHERE user_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
                    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                        sqlite3_bind_text(stmt, 1, token.c_str(), -1, SQLITE_STATIC);
                        if (sqlite3_step(stmt) == SQLITE_ROW) {
                            int buyer_loyalty_points = sqlite3_column_int(stmt, 0);

                            // Calculate cost
                            double total_cost = price * amount;
                            if (buyer_loyalty_points >= loyalty_requirement) {
                                total_cost -= total_cost * loyalty_discount / 100;
                            }

                            // Create the order
                            std::string insert_sql = "INSERT INTO Orders (service_id, buyer_id, amount, total_cost, status) VALUES (?, (SELECT user_id FROM Sessions WHERE auth_token = ?), ?, ?, 'pending')";
                            sqlite3_stmt* insert_stmt;
                            if (sqlite3_prepare_v2(db, insert_sql.c_str(), -1, &insert_stmt, nullptr) == SQLITE_OK) {
                                sqlite3_bind_int(insert_stmt, 1, service_id);
                                sqlite3_bind_text(insert_stmt, 2, token.c_str(), -1, SQLITE_STATIC);
                                sqlite3_bind_int(insert_stmt, 3, amount);
                                sqlite3_bind_double(insert_stmt, 4, total_cost);

                                if (sqlite3_step(insert_stmt) == SQLITE_DONE) {
                                    res.result(http::status::ok);
                                    res.body() = "Order created successfully";
                                } else {
                                    res.result(http::status::internal_server_error);
                                    res.body() = "Error creating order: " + std::string(sqlite3_errmsg(db));
                                }
                                sqlite3_finalize(insert_stmt);
                            } else {
                                res.result(http::status::internal_server_error);
                                res.body() = "Error preparing create order query: " + std::string(sqlite3_errmsg(db));
                            }
                        } else {
                            res.result(http::status::internal_server_error);
                            res.body() = "Error retrieving buyer's loyalty points: " + std::string(sqlite3_errmsg(db));
                        }
                        sqlite3_finalize(stmt);
                    } else {
                        res.result(http::status::internal_server_error);
                        res.body() = "Error retrieving buyer's loyalty points: " + std::string(sqlite3_errmsg(db));
                    }
                } else {
                    res.result(http::status::bad_request);
                    res.body() = "Service not found";
                }
                sqlite3_finalize(stmt);
            } else {
                res.result(http::status::internal_server_error);
                res.body() = "Error retrieving service details: " + std::string(sqlite3_errmsg(db));
            }
        } else {
            res.result(http::status::bad_request);
            res.body() = "Invalid request format";
        }
    } else if (user_type == "seller") {
        res.result(http::status::forbidden);
        res.body() = "Sellers cannot access this feature";
    } else {
        res.result(http::status::forbidden);
        res.body() = "Unknown user type";
    }
}



void handle_order_actions(const std::string& body, const std::string& token, http::response<http::string_body>& res) {
    if (!is_token_valid(token)) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    std::string action;
    std::string order_id;
    auto pos = body.find("action=");
    if (pos != std::string::npos) {
        auto end_pos = body.find('&', pos);
        action = body.substr(pos + 7, end_pos == std::string::npos ? body.size() - pos - 7 : end_pos - pos - 7);
    }
    pos = body.find("order_id=");
    if (pos != std::string::npos) {
        order_id = body.substr(pos + 9);
    }

    if (action == "complete") {
        std::string update_sql = "UPDATE Orders SET status = 'completed' WHERE order_id = ?";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, update_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, order_id.c_str(), -1, SQLITE_STATIC);

            if (sqlite3_step(stmt) == SQLITE_DONE) {
                res.result(http::status::ok);
                res.body() = "Order completed successfully";
            } else {
                res.result(http::status::internal_server_error);
                res.body() = "Error completing order: " + std::string(sqlite3_errmsg(db));
            }
            sqlite3_finalize(stmt);
        } else {
            res.result(http::status::internal_server_error);
            res.body() = "Error preparing complete order query: " + std::string(sqlite3_errmsg(db));
        }
    } else if (action == "cancel") {
        std::string update_sql = "UPDATE Orders SET status = 'cancelled' WHERE order_id = ?";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, update_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, order_id.c_str(), -1, SQLITE_STATIC);

            if (sqlite3_step(stmt) == SQLITE_DONE) {
                res.result(http::status::ok);
                res.body() = "Order cancelled successfully";
            } else {
                res.result(http::status::internal_server_error);
                res.body() = "Error cancelling order: " + std::string(sqlite3_errmsg(db));
            }
            sqlite3_finalize(stmt);
        } else {
            res.result(http::status::internal_server_error);
            res.body() = "Error preparing cancel order query: " + std::string(sqlite3_errmsg(db));
        }
    } else {
        res.result(http::status::bad_request);
        res.body() = "Invalid action specified";
    }
}




// Main request handler function
void handle_request(http::request<http::string_body> req, http::response<http::string_body>& res) {
    std::string body = req.body();
    std::string token = std::string(req[http::field::authorization]);

    if (req.method() == http::verb::post) {
        if (req.target() == "/login") {
            handle_login(body, res);
        } else if (req.target() == "/register") {
            handle_register(body, res);
        } else if (req.target() == "/profile") {
            handle_profile(token, res);
        } else if (req.target() == "/update_profile") {
            handle_update_profile(token, body, res);
        } else if (req.target() == "/logout") {
            handle_logout(token, res);
        } else if (req.target() == "/create_service") {
            handle_create_service(token, body, res);
        } else if (req.target() == "/my_services") {
            handle_my_services(token, res);
        } else if (req.target().find("/delete_service/") == 0) {
            std::string service_id = req.target().substr(16); // Extract service_id from the URL
            handle_delete_service(token, service_id, res);
        } else if (req.target().find("/update_service/") == 0) {
            auto pos = req.target().find('/', 17);
            std::string service_id = req.target().substr(17, pos - 17);
            std::string field_and_value = req.body();
            handle_update_service(token, service_id, field_and_value, res);
        } else {
            res.result(http::status::not_found);
            res.body() = "Endpoint not found";
        }
    } else {
        res.result(http::status::method_not_allowed);
        res.body() = "Method not allowed";
    }
}







void session(tcp::socket socket) {
    try {
        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        http::read(socket, buffer, req);

        http::response<http::string_body> res{http::status::ok, req.version()};
        handle_request(req, res);

        res.set(http::field::content_type, "text/plain");
        res.prepare_payload();
        http::write(socket, res);
    }
    catch (std::exception& e) {
        std::cerr << "Exception in thread: " << e.what() << "\n";
    }
}

void server(boost::asio::io_context& io_context, unsigned short port) {
    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));
    for (;;) {
        tcp::socket socket(io_context);
        acceptor.accept(socket);
        std::thread(session, std::move(socket)).detach();
    }
}

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: http_server <port>\n";
            return 1;
        }

        // Initialize SQLite
        if (sqlite3_open("baza.db", &db) != SQLITE_OK) {
            std::cerr << "Failed to open database\n";
            return 1;
        }

        // Create Sessions table if it doesn't exist
        const char* create_table_sql = R"(
            CREATE TABLE IF NOT EXISTS Sessions (
                session_id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id INTEGER,
                auth_token TEXT,
                FOREIGN KEY(user_id) REFERENCES Korisnici(user_id)
            );
        )";
        char* err_msg = nullptr;
        if (sqlite3_exec(db, create_table_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::cerr << "Failed to create Sessions table: " << err_msg << "\n";
            sqlite3_free(err_msg);
            return 1;
        }

        boost::asio::io_context io_context;
        server(io_context, std::atoi(argv[1]));

        sqlite3_close(db);
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
