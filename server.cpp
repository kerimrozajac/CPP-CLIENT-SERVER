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
    
    // Remove "Bearer " prefix if it exists
    std::string clean_token = token;
    if (clean_token.substr(0, 7) == "Bearer ") {
        clean_token = clean_token.substr(7);
    }

    // Prepare the SQL statement
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing token validation query: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    // Bind the token to the query
    if (sqlite3_bind_text(stmt, 1, clean_token.c_str(), -1, SQLITE_STATIC) != SQLITE_OK) {
        std::cerr << "Error binding token: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }

    // Execute the query and check if a row was returned
    int result = sqlite3_step(stmt);
    bool valid = (result == SQLITE_ROW);

    // Debug output
    if (result == SQLITE_DONE) {
        std::cerr << "TEST!! No matching rows found" << std::endl;
    } else if (result != SQLITE_ROW && result != SQLITE_DONE) {
        std::cerr << "TEST!! Error executing query: " << sqlite3_errmsg(db) << std::endl;
    }

    // Finalize the statement
    sqlite3_finalize(stmt);

    // More debug output
    if (valid) {
        std::cerr << "TEST!! VALIDNO" << std::endl;
    } else {
        std::cerr << "TEST!! NIJE VALIDNO" << std::endl;
    }

    return valid;
}


// Function to get user ID from token
int get_user_id_by_token(const std::string& token) {
    // Remove "Bearer " prefix if present
    std::string clean_token = token;
    const std::string bearer_prefix = "Bearer ";
    if (clean_token.find(bearer_prefix) == 0) {
        clean_token.erase(0, bearer_prefix.length());
    }

    std::string sql = "SELECT user_id FROM Sessions WHERE auth_token = ?";
    sqlite3_stmt* stmt;
    int user_id = -1; // Default to -1 if token is not valid or not found

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, clean_token.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            user_id = sqlite3_column_int(stmt, 0);
        }

        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Error preparing query: " << sqlite3_errmsg(db) << std::endl;
    }

    return user_id;
}


int get_seller_id_by_service_id(int service_id) {
    std::string sql = "SELECT seller_id FROM Usluge WHERE service_id = ?";
    sqlite3_stmt* stmt;
    int seller_id = -1;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, service_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            seller_id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "Error retrieving seller_id: " << sqlite3_errmsg(db) << std::endl;
    }
    return seller_id;
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
    } else {
        res.result(http::status::bad_request);
        res.body() = "Username not found in request";
        return;
    }
    pos = body.find("password=");
    if (pos != std::string::npos) {
        password = body.substr(pos + 9);
    } else {
        res.result(http::status::bad_request);
        res.body() = "Password not found in request";
        return;
    }

    // SQL query to fetch user ID and type
    std::string sql = "SELECT user_id, user_type FROM Korisnici WHERE username = ? AND password = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing query: " << sqlite3_errmsg(db) << std::endl;
        res.result(http::status::internal_server_error);
        res.body() = "Database query error";
        return;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int user_id = sqlite3_column_int(stmt, 0);
        std::string user_type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string token = generate_token(32);

        // Remove all existing sessions for this user
        std::string delete_sql = "DELETE FROM Sessions WHERE user_id = ?";
        sqlite3_stmt* delete_stmt;
        if (sqlite3_prepare_v2(db, delete_sql.c_str(), -1, &delete_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Error preparing delete statement: " << sqlite3_errmsg(db) << std::endl;
            res.result(http::status::internal_server_error);
            res.body() = "Error preparing session delete";
            sqlite3_finalize(stmt);
            return;
        }

        sqlite3_bind_int(delete_stmt, 1, user_id);
        if (sqlite3_step(delete_stmt) != SQLITE_DONE) {
            std::cerr << "Error removing old sessions: " << sqlite3_errmsg(db) << std::endl;
            res.result(http::status::internal_server_error);
            res.body() = "Error removing old sessions";
            sqlite3_finalize(delete_stmt);
            sqlite3_finalize(stmt);
            return;
        }
        sqlite3_finalize(delete_stmt);

        // Insert the new session into the Sessions table
        std::string insert_sql = "INSERT INTO Sessions (user_id, auth_token) VALUES (?, ?)";
        sqlite3_stmt* insert_stmt;
        if (sqlite3_prepare_v2(db, insert_sql.c_str(), -1, &insert_stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Error preparing insert statement: " << sqlite3_errmsg(db) << std::endl;
            res.result(http::status::internal_server_error);
            res.body() = "Error preparing session insert";
            sqlite3_finalize(stmt);
            return;
        }

        sqlite3_bind_int(insert_stmt, 1, user_id);
        sqlite3_bind_text(insert_stmt, 2, token.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
            std::cerr << "Error creating session: " << sqlite3_errmsg(db) << std::endl;
            res.result(http::status::internal_server_error);
            res.body() = "Error creating session";
            sqlite3_finalize(insert_stmt);
            sqlite3_finalize(stmt);
            return;
        }
        sqlite3_finalize(insert_stmt);

        // Create JSON response
        json::object response_body;
        response_body["success"] = true;
        response_body["token"] = token;
        response_body["user_type"] = user_type;

        std::string response_str;
        try {
            response_str = json::serialize(response_body);
        } catch (const std::exception& e) {
            std::cerr << "Error serializing JSON: " << e.what() << std::endl;
            res.result(http::status::internal_server_error);
            res.body() = "Error generating response";
            sqlite3_finalize(stmt);
            return;
        }

        res.result(http::status::ok);
        res.body() = response_str;
    } else {
        std::cerr << "Invalid username or password" << std::endl;
        res.result(http::status::unauthorized);
        res.body() = "Invalid username or password";
    }

    sqlite3_finalize(stmt);
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

// Helper function to extract a field value from the request body
std::string get_field_value(const std::string& field_name, const std::string& body) {
    auto pos = body.find(field_name + "=");
    if (pos != std::string::npos) {
        auto end_pos = body.find('&', pos);
        return url_decode(body.substr(pos + field_name.size() + 1, end_pos == std::string::npos ? body.size() - pos - field_name.size() - 1 : end_pos - pos - field_name.size() - 1));
    }
    return std::string{};
}

void handle_update_profile(const std::string& token, const std::string& body, http::response<http::string_body>& res) {
    if (is_token_valid(token)) {
        // Extract fields from body to update
        std::string username = get_field_value("username", body);
        std::string password = get_field_value("password", body);
        std::string email = get_field_value("email", body);
        std::string user_type = get_field_value("user_type", body);

        // Log extracted values for debugging
        std::cerr << "Extracted values - username: " << username 
                  << ", password: " << password 
                  << ", email: " << email 
                  << ", user_type: " << user_type << std::endl;

        // Validate user_type
        if (!user_type.empty() && user_type != "buyer" && user_type != "seller") {
            res.result(http::status::bad_request);
            res.body() = "Invalid user_type value. Must be 'buyer' or 'seller'.";
            return;
        }

        // Prepare SQL statement dynamically based on provided fields
        std::string sql = "UPDATE Korisnici SET ";
        bool first_field = true;

        if (!username.empty()) {
            sql += "username = ?";
            first_field = false;
        }
        if (!password.empty()) {
            if (!first_field) sql += ", ";
            sql += "password = ?";
            first_field = false;
        }
        if (!email.empty()) {
            if (!first_field) sql += ", ";
            sql += "email = ?";
            first_field = false;
        }
        if (!user_type.empty()) {
            if (!first_field) sql += ", ";
            sql += "user_type = ?";
        }
        sql += " WHERE user_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Error preparing update profile query: " << sqlite3_errmsg(db) << std::endl;
            res.result(http::status::internal_server_error);
            res.body() = "Error preparing update profile query: " + std::string(sqlite3_errmsg(db));
            return;
        }

        int bind_index = 1;
        if (!username.empty()) {
            sqlite3_bind_text(stmt, bind_index++, username.c_str(), -1, SQLITE_STATIC);
        }
        if (!password.empty()) {
            sqlite3_bind_text(stmt, bind_index++, password.c_str(), -1, SQLITE_STATIC);
        }
        if (!email.empty()) {
            sqlite3_bind_text(stmt, bind_index++, email.c_str(), -1, SQLITE_STATIC);
        }
        if (!user_type.empty()) {
            sqlite3_bind_text(stmt, bind_index++, user_type.c_str(), -1, SQLITE_STATIC);
        }
        sqlite3_bind_text(stmt, bind_index, token.c_str(), -1, SQLITE_STATIC);

        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            res.result(http::status::ok);
            res.body() = "Profile updated successfully";
        } else {
            std::cerr << "Error updating profile: " << sqlite3_errmsg(db) << std::endl;
            res.result(http::status::internal_server_error);
            res.body() = "Error updating profile: " + std::string(sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    } else {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
    }
}






// Handle logout request
void handle_logout(const std::string& token, http::response<http::string_body>& res) {
    // Remove "Bearer " prefix if it exists
    std::string clean_token = token;
    if (clean_token.substr(0, 7) == "Bearer ") {
        clean_token = clean_token.substr(7);
    }

    if (is_token_valid(clean_token)) {
        // Delete the session from the Sessions table
        std::string delete_sql = "DELETE FROM Sessions WHERE auth_token = ?";
        sqlite3_stmt* delete_stmt;
        if (sqlite3_prepare_v2(db, delete_sql.c_str(), -1, &delete_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(delete_stmt, 1, clean_token.c_str(), -1, SQLITE_STATIC);
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

void handle_delete_service(const std::string& token, const std::string& body, http::response<http::string_body>& res) {
    // Validate the token
    if (!is_token_valid(token)) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    // Extract the service_id from the body
    std::string service_id_str = get_field_value("service_id", body);
    int service_id;
    try {
        service_id = std::stoi(service_id_str);
    } catch (const std::invalid_argument&) {
        res.result(http::status::bad_request);
        res.body() = "Invalid service ID format";
        return;
    }

    // Prepare SQL statement for deleting the service
    std::string sql = "DELETE FROM Usluge WHERE service_id = ? AND seller_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.result(http::status::internal_server_error);
        res.body() = "Error preparing SQL statement: " + std::string(sqlite3_errmsg(db));
        return;
    }

    // Bind parameters to the SQL statement
    sqlite3_bind_int(stmt, 1, service_id);
    sqlite3_bind_text(stmt, 2, token.c_str(), -1, SQLITE_STATIC);

    // Execute the SQL statement
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        if (sqlite3_changes(db) > 0) {
            res.result(http::status::ok);
            res.body() = "Service deleted successfully";
        } else {
            res.result(http::status::not_found);
            res.body() = "Service not found or not owned by the user";
        }
    } else {
        res.result(http::status::internal_server_error);
        res.body() = "Error deleting service: " + std::string(sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
}

void handle_update_service(const std::string& token, const std::string& body, http::response<http::string_body>& res) {
    if (!is_token_valid(token)) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    // Parse the body to extract service_id and field_name=value
    auto delimiter_pos = body.find('&');
    if (delimiter_pos == std::string::npos) {
        res.result(http::status::bad_request);
        res.body() = "Invalid body format";
        return;
    }

    // Extract service_id
    std::string service_id_str = body.substr(0, delimiter_pos);
    // Extract field_name=value
    std::string field_and_value = body.substr(delimiter_pos + 1);

    // Extract field name and new value
    auto field_delim_pos = field_and_value.find('=');
    if (field_delim_pos == std::string::npos) {
        res.result(http::status::bad_request);
        res.body() = "Invalid field_and_value format";
        return;
    }

    std::string field_name = field_and_value.substr(0, field_delim_pos);
    std::string new_value = field_and_value.substr(field_delim_pos + 1);

    // Prepare SQL statement
    std::string sql = "UPDATE Usluge SET " + field_name + " = ? WHERE service_id = ? AND seller_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.result(http::status::internal_server_error);
        res.body() = "Error preparing SQL statement: " + std::string(sqlite3_errmsg(db));
        return;
    }

    // Bind parameters
    sqlite3_bind_text(stmt, 1, new_value.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, std::stoi(service_id_str));
    sqlite3_bind_text(stmt, 3, token.c_str(), -1, SQLITE_STATIC);

    // Execute the SQL statement
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        res.result(http::status::ok);
        res.body() = "Service updated successfully";
    } else {
        res.result(http::status::internal_server_error);
        res.body() = "Error updating service: " + std::string(sqlite3_errmsg(db));
    }

    // Finalize the statement
    sqlite3_finalize(stmt);
}




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
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.result(http::status::internal_server_error);
        res.body() = "Error preparing SQL statement: " + std::string(sqlite3_errmsg(db));
        return;
    }

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
}


void handle_make_order(const std::string& token, const std::string& body, http::response<http::string_body>& res) {
    
    std::cerr << "HANDLE MAKE ORDER INITIATED OK";
    std::cerr << "TOKEN: " << token;
    
    // Get the user ID from the token
    int user_id = get_user_id_by_token(token);
    if (user_id == -1) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    std::cerr << "USER ID:" << user_id;

    // Extract service ID and quantity from the request body
    auto get_field_value = [&](const std::string& field_name) {
        auto pos = body.find(field_name + "=");
        if (pos != std::string::npos) {
            auto end_pos = body.find('&', pos);
            return url_decode(body.substr(pos + field_name.size() + 1, end_pos == std::string::npos ? body.size() - pos - field_name.size() - 1 : end_pos - pos - field_name.size() - 1));
        }
        return std::string{};
    };

    std::string service_id_str = get_field_value("service_id");
    std::string quantity_str = get_field_value("quantity");

    if (service_id_str.empty() || quantity_str.empty()) {
        res.result(http::status::bad_request);
        res.body() = "Missing service_id or quantity";
        return;
    }

    int service_id = std::stoi(service_id_str);
    int quantity = std::stoi(quantity_str);

    std::cerr << "SERVICE ID:" << service_id << "\n";
    std::cerr << "QUANTITY:" << quantity << "\n";

    // Get the seller_id by service_id
    int seller_id = get_seller_id_by_service_id(service_id);
    if (seller_id == -1) {
        res.result(http::status::bad_request);
        res.body() = "Invalid service_id";
        return;
    }

    // Check service capacity and get price
    std::string sql = "SELECT capacity, price, loyalty_discount, loyalty_requirement FROM Usluge WHERE service_id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, service_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int capacity = sqlite3_column_int(stmt, 0);
            double price = sqlite3_column_double(stmt, 1);
            double loyalty_discount = sqlite3_column_double(stmt, 2);
            int loyalty_requirement = sqlite3_column_int(stmt, 3);

            if (quantity > capacity) {
                res.result(http::status::bad_request);
                res.body() = "Requested quantity exceeds service capacity";
                std::cerr << "Requested quantity exceeds service capacity" << "\n";
                sqlite3_finalize(stmt);
                return;
            }

            std::cerr << "All good so far" << "\n";

            // Check buyer's loyalty points in the Lojalnosti table
            sql = "SELECT loyalty_points FROM Lojalnosti WHERE seller_id = ? AND buyer_id = ?";
            sqlite3_stmt* loyalty_stmt;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &loyalty_stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(loyalty_stmt, 1, seller_id);
                sqlite3_bind_int(loyalty_stmt, 2, user_id);
                if (sqlite3_step(loyalty_stmt) == SQLITE_ROW) {
                    int buyer_loyalty_points = sqlite3_column_int(loyalty_stmt, 0);

                    std::cerr << "BUYER LOYALTY POINTS: " << buyer_loyalty_points << "\n";

                    // Calculate cost
                    double total_cost = price * quantity;
                    if (buyer_loyalty_points >= loyalty_requirement) {
                        total_cost -= total_cost * loyalty_discount / 100;
                    }

                    std::cerr << "TOTAL COST: " << total_cost << "\n";

                    // Create the order
                    std::string insert_sql = "INSERT INTO Narudzbe (service_id, buyer_id, seller_id, quantity, cost, order_status) VALUES (?, ?, ?, ?, ?, 'pending')";
                    sqlite3_stmt* insert_stmt;
                    if (sqlite3_prepare_v2(db, insert_sql.c_str(), -1, &insert_stmt, nullptr) == SQLITE_OK) {
                        std::cerr << "if sqlite3 all good: " << total_cost << service_id << user_id << seller_id << quantity << "\n";
                        sqlite3_bind_int(insert_stmt, 1, service_id);
                        sqlite3_bind_int(insert_stmt, 2, user_id);
                        sqlite3_bind_int(insert_stmt, 3, seller_id);
                        sqlite3_bind_int(insert_stmt, 4, quantity);
                        sqlite3_bind_double(insert_stmt, 5, total_cost);

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
                sqlite3_finalize(loyalty_stmt);
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
            sql = "SELECT order_id, service_id, quantity, status FROM Narudzbe WHERE buyer_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
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
            std::string service_id_str, quantity_str;
            auto get_field_value = [&](const std::string& field_name) {
                auto pos = body.find(field_name + "=");
                if (pos != std::string::npos) {
                    auto end_pos = body.find('&', pos);
                    return url_decode(body.substr(pos + field_name.size() + 1, end_pos == std::string::npos ? body.size() - pos - field_name.size() - 1 : end_pos - pos - field_name.size() - 1));
                }
                return std::string{};
            };

            service_id_str = get_field_value("service_id");
            quantity_str = get_field_value("quantity");

            int service_id = std::stoi(service_id_str);
            int quantity = std::stoi(quantity_str);

            // Check service capacity and get price
            sql = "SELECT capacity, price, loyalty_discount, loyalty_requirement FROM Usluge WHERE service_id = ?";
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, service_id);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    int capacity = sqlite3_column_int(stmt, 0);
                    double price = sqlite3_column_double(stmt, 1);
                    double loyalty_discount = sqlite3_column_double(stmt, 2);
                    int loyalty_requirement = sqlite3_column_int(stmt, 3);

                    if (quantity > capacity) {
                        res.result(http::status::bad_request);
                        res.body() = "Requested quantity exceeds service capacity";
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
                            double total_cost = price * quantity;
                            if (buyer_loyalty_points >= loyalty_requirement) {
                                total_cost -= total_cost * loyalty_discount / 100;
                            }

                            // Create the order
                            std::string insert_sql = "INSERT INTO Orders (service_id, buyer_id, quantity, total_cost, status) VALUES (?, (SELECT user_id FROM Sessions WHERE auth_token = ?), ?, ?, 'pending')";
                            sqlite3_stmt* insert_stmt;
                            if (sqlite3_prepare_v2(db, insert_sql.c_str(), -1, &insert_stmt, nullptr) == SQLITE_OK) {
                                sqlite3_bind_int(insert_stmt, 1, service_id);
                                sqlite3_bind_text(insert_stmt, 2, token.c_str(), -1, SQLITE_STATIC);
                                sqlite3_bind_int(insert_stmt, 3, quantity);
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

void handle_loyalty_buyers(const std::string& token, http::response<http::string_body>& res) {
    // Get the user ID from the token
    int user_id = get_user_id_by_token(token);
    if (user_id == -1) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    // Query to retrieve seller_id, seller_name, and loyalty points for the buyer
    std::string sql = R"(
        SELECT L.seller_id, K.username, L.loyalty_points
        FROM Lojalnosti L
        JOIN Korisnici K ON L.seller_id = K.user_id
        WHERE L.buyer_id = ?
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing query: " << sqlite3_errmsg(db) << std::endl;
        res.result(http::status::internal_server_error);
        res.body() = "Database query error";
        return;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    // Create JSON response object
    json::array sellers_array;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json::object seller_info;
        seller_info["seller_id"] = sqlite3_column_int(stmt, 0);
        seller_info["seller_name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        seller_info["loyalty_points"] = sqlite3_column_int(stmt, 2);
        sellers_array.push_back(seller_info);
    }
    sqlite3_finalize(stmt);

    // Create the JSON response
    json::object response_body;
    response_body["sellers"] = sellers_array;

    std::string response_str;
    try {
        response_str = json::serialize(response_body);
    } catch (const std::exception& e) {
        std::cerr << "Error serializing JSON: " << e.what() << std::endl;
        res.result(http::status::internal_server_error);
        res.body() = "Error generating response";
        return;
    }

    res.result(http::status::ok);
    res.body() = response_str;
}

void handle_loyalty_sellers(const std::string& token, http::response<http::string_body>& res) {
    // Get the seller ID from the token
    int seller_id = get_user_id_by_token(token);
    if (seller_id == -1) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    // Query to retrieve buyer_id, buyer_name, and loyalty points for the seller
    std::string sql = R"(
        SELECT L.buyer_id, K.username, L.loyalty_points
        FROM Lojalnosti L
        JOIN Korisnici K ON L.buyer_id = K.user_id
        WHERE L.seller_id = ?
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing query: " << sqlite3_errmsg(db) << std::endl;
        res.result(http::status::internal_server_error);
        res.body() = "Database query error";
        return;
    }

    sqlite3_bind_int(stmt, 1, seller_id);

    // Create JSON response object
    json::array buyers_array;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json::object buyer_info;
        buyer_info["buyer_id"] = sqlite3_column_int(stmt, 0);
        buyer_info["buyer_name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        buyer_info["loyalty_points"] = sqlite3_column_int(stmt, 2);
        buyers_array.push_back(buyer_info);
    }
    sqlite3_finalize(stmt);

    // Create the JSON response
    json::object response_body;
    response_body["buyers"] = buyers_array;

    std::string response_str;
    try {
        response_str = json::serialize(response_body);
    } catch (const std::exception& e) {
        std::cerr << "Error serializing JSON: " << e.what() << std::endl;
        res.result(http::status::internal_server_error);
        res.body() = "Error generating response";
        return;
    }

    res.result(http::status::ok);
    res.body() = response_str;
}



// Handle "View My Orders" request
void handle_my_orders(const std::string& token, http::response<http::string_body>& res) {
    
    std::cerr << "TEST!! HANDLE MY ORDERS" << std::endl;
    // Retrieve the user ID from the token
    int user_id = get_user_id_by_token(token);
    std::cerr << "TEST!! USER ID:" << user_id << std::endl;


    if (user_id == -1) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    // Query to retrieve orders for the logged-in user
    std::string sql = "SELECT order_id, service_id, quantity, order_status FROM Narudzbe WHERE buyer_id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing query: " << sqlite3_errmsg(db) << std::endl;
        res.result(http::status::internal_server_error);
        res.body() = "Database query error";
        return;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    // Create JSON response object
    json::array orders_array;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json::object order;
        order["order_id"] = sqlite3_column_int(stmt, 0);
        order["service_id"] = sqlite3_column_int(stmt, 1);
        order["quantity"] = sqlite3_column_int(stmt, 2);
        order["order_status"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        orders_array.push_back(order);
    }
    sqlite3_finalize(stmt);

    // Create the JSON response
    json::object response_body;
    response_body["orders"] = orders_array;

    std::string response_str;
    try {
        response_str = json::serialize(response_body);
    } catch (const std::exception& e) {
        std::cerr << "Error serializing JSON: " << e.what() << std::endl;
        res.result(http::status::internal_server_error);
        res.body() = "Error generating response";
        return;
    }

    res.result(http::status::ok);
    res.body() = response_str;
}


void handle_all_services(const std::string& token, http::response<http::string_body>& res) {
    // Query to retrieve all services
    std::string sql = "SELECT service_id, service_name, price, capacity, working_hours, service_type FROM Usluge";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Error preparing query: " << sqlite3_errmsg(db) << std::endl;
        res.result(http::status::internal_server_error);
        res.body() = "Database query error";
        return;
    }

    // Create JSON response object
    json::array services_array;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json::object service;
        service["service_id"] = sqlite3_column_int(stmt, 0);
        service["service_name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        service["price"] = sqlite3_column_double(stmt, 2);
        service["capacity"] = sqlite3_column_int(stmt, 3);
        service["working_hours"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        service["service_type"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        services_array.push_back(service);
    }
    sqlite3_finalize(stmt);

    // Create the JSON response
    json::object response_body;
    response_body["services"] = services_array;

    std::string response_str;
    try {
        response_str = json::serialize(response_body);
    } catch (const std::exception& e) {
        std::cerr << "Error serializing JSON: " << e.what() << std::endl;
        res.result(http::status::internal_server_error);
        res.body() = "Error generating response";
        return;
    }

    res.result(http::status::ok);
    res.body() = response_str;
}

void handle_update_order_status(
    const std::string& token,
    const std::string& body,
    http::response<http::string_body>& res
) {
    if (!is_token_valid(token)) {
        res.result(http::status::unauthorized);
        res.body() = "Invalid or missing token";
        return;
    }

    // Parse the body to extract order_id and status
    auto delimiter_pos = body.find('&');
    if (delimiter_pos == std::string::npos) {
        res.result(http::status::bad_request);
        res.body() = "Invalid body format";
        return;
    }

    // Extract order_id
    std::string order_id_str = body.substr(0, delimiter_pos);
    // Extract status
    std::string status = body.substr(delimiter_pos + 1);

    // Prepare SQL statement
    std::string sql = "UPDATE Orders SET status = ? WHERE order_id = ? AND buyer_id = (SELECT user_id FROM Sessions WHERE auth_token = ?)";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        res.result(http::status::internal_server_error);
        res.body() = "Error preparing SQL statement: " + std::string(sqlite3_errmsg(db));
        return;
    }

    // Bind parameters
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, std::stoi(order_id_str));
    sqlite3_bind_text(stmt, 3, token.c_str(), -1, SQLITE_STATIC);

    // Execute the SQL statement
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        res.result(http::status::ok);
        res.body() = "Order status updated successfully";
    } else {
        res.result(http::status::internal_server_error);
        res.body() = "Error updating order status: " + std::string(sqlite3_errmsg(db));
    }

    // Finalize the statement
    sqlite3_finalize(stmt);
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
    std::cerr << "HANDLE REQUEST TEST TARGET" << req.target() << "\n";
    std::cerr << "HANDLE REQUEST TEST METHOD" << req.method() << "\n";
    std::cerr << "HANDLE REQUEST BODY METHOD" << req.body() << "\n";

    std::cerr << "TOKEN: " << token;


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
        } else if (req.target() == "/make_order") {
            handle_make_order(token, body, res);
        
        } else if (req.target() == "/delete_service") {
            handle_delete_service(token, body, res);

        } else if (req.target() == "/update_service") {
            handle_update_service(token, body, res);
        } else if (req.target() == "/update_order_status") {
        handle_update_order_status(token, body, res);

        /*
        } else if (req.target().find("/delete_service/") == 0) {
            std::string service_id = req.target().substr(16); // Extract service_id from the URL
            handle_delete_service(token, service_id, res);
        

        } else if (req.target().find("/update_service/") == 0) {
            auto pos = req.target().find('/', 17);
            std::string service_id = req.target().substr(17, pos - 17);
            std::string field_and_value = req.body();
            handle_update_service(token, service_id, field_and_value, res);
*/

        } else {
            res.result(http::status::not_found);
            res.body() = "Endpoint not found";
        }
    } else if (req.method() == http::verb::get) {
            if (req.target() == "/my_orders") {
            std::cerr << "HANDLE REQUEST OK";
            handle_my_orders(token, res);
            } 
            else if (req.target() == "/all_services") {
            handle_all_services(token, res);
            }
            else if (req.target() == "/loyalty/buyers") {
            handle_loyalty_buyers(token, res);
            }
            else if (req.target() == "/loyalty/sellers") {
            handle_loyalty_sellers(token, res);
            }
            else if (req.target() == "/my_services") {
            handle_my_services(token, res);
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
