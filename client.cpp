#include <curl/curl.h>
#include <iostream>
#include <string>
#include <sstream>

size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append((char*)contents, total_size);
    return total_size;
}

// Function to send HTTP requests
long send_request(const std::string& host, int port, const std::string& endpoint, 
                  const std::string& data, std::string& response_data, 
                  const std::string* token = nullptr, bool is_get = true) {
    CURL* curl;
    CURLcode res;
    long response_code;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        std::string url = "http://" + host + ":" + std::to_string(port) + endpoint;

        if (is_get) {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            if (!data.empty()) {
                url += (url.find('?') == std::string::npos ? "?" : "&") + data;
            }
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        } else {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

        if (token) {
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, ("Authorization: " + *token).c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            response_code = -1; // Indicate error
        } else {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            std::cout << "HTTP Response Code: " << response_code << std::endl;
        }

        curl_easy_cleanup(curl);
    } else {
        std::cerr << "Failed to initialize CURL" << std::endl;
        response_code = -1; // Indicate error
    }

    curl_global_cleanup();
    return response_code;
}

// Function to parse token and user type from the response data
std::pair<std::string, std::string> parse_login_response(const std::string& response_data) {
    std::string token;
    std::string user_type;

    // Find the token
    size_t token_pos = response_data.find("\"token\":\"");
    if (token_pos != std::string::npos) {
        size_t token_start = token_pos + 9;
        size_t token_end = response_data.find("\"", token_start);
        token = response_data.substr(token_start, token_end - token_start);
    }

    // Find the user type
    size_t user_type_pos = response_data.find("\"user_type\":\"");
    if (user_type_pos != std::string::npos) {
        size_t user_type_start = user_type_pos + 13;
        size_t user_type_end = response_data.find("\"", user_type_start);
        user_type = response_data.substr(user_type_start, user_type_end - user_type_start);
    }

    return {token, user_type};
}

// Function to handle login requests and extract the token and user type
std::pair<std::string, std::string> send_login_request(const std::string& host, int port, const std::string& username, const std::string& password) {
    std::string login_data = "username=" + username + "&password=" + password;
    std::string response_data;

    send_request(host, port, "/login", login_data, response_data, nullptr, false); // POST request

    // Parse the response data to extract token and user type
    return parse_login_response(response_data);
}

// Function to handle registration requests
void send_registration_request(const std::string& host, int port, const std::string& username, const std::string& password, const std::string& email) {
    // Include email in the registration data
    std::string registration_data = "username=" + username + "&password=" + password + "&email=" + email;
    std::string response_data;

    // Send the POST request
    long response_code = send_request(host, port, "/register", registration_data, response_data, nullptr, false);

    if (response_code == 200) {
        std::cout << "Registration successful.\n";
    } else {
        std::cerr << "Registration failed. Response Code: " << response_code << "\n";
    }
}


// Function to get seller orders
void get_seller_orders(const std::string& host, int port, const std::string& token) {
    // Create a string to store the response data
    std::string response_data;

    // Send a request to get the seller orders
    long response_code = send_request(host, port, "/seller_orders", "", response_data, &token); // GET request

    // Check the response code and output the result
    if (response_code == 200) {
        std::cout << "Seller Orders: " << response_data << std::endl;
    } else {
        std::cerr << "Failed to retrieve seller orders. Response Code: " << response_code << "\n";
    }
}




// Function to handle profile menu
void handle_profile_menu(const std::string& host, int port, const std::string& token, const std::string& current_profile_type) {
    while (true) {
        std::cout << "\nProfile Menu:\n";
        std::cout << "1. Edit Profile\n";
        std::cout << "2. Profile Type\n";  // Added option to manage profile type
        std::cout << "3. Delete Profile\n";
        std::cout << "4. Back\n";

        int profile_choice;
        std::cin >> profile_choice;
        std::cin.ignore(); // Ignore leftover newline character

        if (profile_choice == 1) {
            while (true) {
                std::cout << "\nEdit Profile Menu:\n";
                std::cout << "1. Edit Username\n";
                std::cout << "2. Change Password\n";
                std::cout << "3. Change Email\n";
                std::cout << "4. Back\n";

                int edit_choice;
                std::cin >> edit_choice;
                std::cin.ignore(); // Ignore leftover newline character

                std::string field, new_value;
                if (edit_choice == 1) field = "username";
                else if (edit_choice == 2) field = "password";
                else if (edit_choice == 3) field = "email";
                else if (edit_choice == 4) break;
                else {
                    std::cerr << "Invalid choice. Please try again.\n";
                    continue;
                }

                std::cout << "Enter new " << field << ": ";
                std::getline(std::cin, new_value);

                std::string update_data = field + "=" + new_value;
                std::string response_data;

                long response_code = send_request(host, port, "/update_profile", update_data, response_data, &token, false); // POST request
                if (response_code == 200) {
                    std::cout << "Profile updated successfully.\n";
                } else {
                    std::cerr << "Failed to update profile. Response Code: " << response_code << "\n";
                }
            }
        } else if (profile_choice == 2) {  // Added profile type option
            while (true) {
                std::cout << "\nProfile Type Menu:\n";
                std::cout << "Your current Profile type is: " << current_profile_type << "\n";
                std::cout << "1. Change Profile type to " << (current_profile_type == "seller" ? "buyer" : "seller") << "\n"; // Option to change type
                std::cout << "2. Exit\n";

                int type_choice;
                std::cin >> type_choice;
                std::cin.ignore(); // Ignore leftover newline character

                if (type_choice == 1) {
                    std::string new_profile_type = current_profile_type == "seller" ? "buyer" : "seller";
                    std::string update_data = "user_type=" + new_profile_type;
                    std::string response_data;

                    long response_code = send_request(host, port, "/update_profile", update_data, response_data, &token, false); // POST request

                    if (response_code == 200) {
                        std::cout << "Profile type changed successfully to " << new_profile_type << ".\n";
                        break;  // Exit after change
                    } else {
                        std::cerr << "Failed to change profile type. Response Code: " << response_code << "\n";
                    }
                } else if (type_choice == 2) {
                    break; // Exit Profile Type Menu
                } else {
                    std::cerr << "Invalid choice. Please try again.\n";
                }
            }
        } else if (profile_choice == 3) {
            std::string response_data;
            long response_code = send_request(host, port, "/delete_profile", "", response_data, &token, false); // DELETE request
            if (response_code == 200) {
                std::cout << "Profile deleted successfully.\n";
                return; // Exit after deletion
            } else {
                std::cerr << "Failed to delete profile. Response Code: " << response_code << "\n";
            }
        } else if (profile_choice == 4) {
            break; // Return to dashboard menu
        } else {
            std::cerr << "Invalid choice. Please try again.\n";
        }
    }
}


// Function to handle orders menu for sellers
void handle_seller_orders_menu(const std::string& host, int port, const std::string& token) {
    while (true) {
        std::cout << "\nOrders Menu (Seller):\n";
        std::cout << "1. Set Order as In Progress\n";
        std::cout << "2. Set Order as Delivered\n";
        std::cout << "3. Set Order as Cancelled\n";
        std::cout << "4. Set Order as Complete\n";
        std::cout << "5. Back\n";

        int orders_choice;
        std::cin >> orders_choice;
        std::cin.ignore(); // Ignore leftover newline character

        if (orders_choice >= 1 && orders_choice <= 4) {
            std::string order_id;
            std::cout << "Enter Order ID: ";
            std::getline(std::cin, order_id);

            std::string status;
            if (orders_choice == 1) status = "In Progress";
            else if (orders_choice == 2) status = "Delivered";
            else if (orders_choice == 3) status = "Cancelled";
            else if (orders_choice == 4) status = "Complete";

            std::string order_data = "order_id=" + order_id + "&status=" + status;
            std::string response_data;

            long response_code = send_request(host, port, "/update_order_status", order_data, response_data, &token, false); // POST request
            if (response_code == 200) {
                std::cout << "Order status updated to " << status << " successfully.\n";
            } else {
                std::cerr << "Failed to update order status. Response Code: " << response_code << "\n";
            }
        } else if (orders_choice == 5) {
            break; // Return to dashboard menu
        } else {
            std::cerr << "Invalid choice. Please try again.\n";
        }
    }
}

// Function to handle orders menu for buyers
// Function to handle orders menu for buyers
void handle_buyer_orders_menu(const std::string& host, int port, const std::string& token) {
    while (true) {
        std::cout << "\nOrders Menu (Buyer):\n";
        std::cout << "1. View My Orders\n";
        std::cout << "2. Make an Order\n";
        std::cout << "3. Back\n";

        int orders_choice;
        std::cin >> orders_choice;
        std::cin.ignore(); // Ignore leftover newline character

        if (orders_choice == 1) {
            // View My Orders
            while (true) {
                std::string response_data;
                send_request(host, port, "/my_orders", "", response_data, &token); // GET request
                std::cout << "My Orders: " << response_data << std::endl;

                // Submenu for completing or canceling orders
                std::cout << "1. Complete an Order\n";
                std::cout << "2. Cancel an Order\n";
                std::cout << "3. Back to Orders Menu\n";

                int action_choice;
                std::cin >> action_choice;
                std::cin.ignore(); // Ignore leftover newline character

                if (action_choice == 1 || action_choice == 2) {
                    std::string order_id;
                    std::cout << "Enter Order ID: ";
                    std::getline(std::cin, order_id);

                    std::string status = (action_choice == 1) ? "Complete" : "Cancelled";
                    std::string order_data = "order_id=" + order_id + "&status=" + status;
                    std::string response_data;

                    long response_code = send_request(host, port, "/update_order_status", order_data, response_data, &token, false); // POST request
                    if (response_code == 200) {
                        std::cout << "Order status updated: " << status << std::endl;
                    } else {
                        std::cerr << "Failed to update order status. Response Code: " << response_code << "\n";
                    }
                } else if (action_choice == 3) {
                    break; // Return to Orders Menu
                } else {
                    std::cerr << "Invalid choice. Please try again.\n";
                }
            }
        } else if (orders_choice == 2) {
            // Make an Order
            std::string service_id, quantity;
            std::cout << "Enter Service ID: ";
            std::getline(std::cin, service_id);
            std::cout << "Enter Amount: ";
            std::getline(std::cin, quantity);

            std::string order_data = "service_id=" + service_id + "&quantity=" + quantity;
            std::string response_data;
            long response_code = send_request(host, port, "/make_order", order_data, response_data, &token, false); // POST request

            if (response_code == 201) {
                std::cout << "Order created successfully.\n";
            } else {
                std::cerr << "Failed to create order. Response Code: " << response_code << "\n";
            }
        } else if (orders_choice == 3) {
            break; // Return to dashboard menu
        } else {
            std::cerr << "Invalid choice. Please try again.\n";
        }
    }
}




// Function to handle services menu
void handle_services_menu(const std::string& host, int port, const std::string& token, const std::string& user_type) {
    while (true) {
        std::cout << "\nServices Menu:\n";

        if (user_type == "seller") {
            std::cout << "1. Create Service\n";
            std::cout << "2. View My Services\n";
            std::cout << "3. Delete Service\n";
            std::cout << "4. Update Service\n";
            std::cout << "5. Back\n";
        } else if (user_type == "buyer") {
            std::cout << "1. View All Services\n";
            std::cout << "2. Back\n";
        }

        int services_choice;
        std::cin >> services_choice;
        std::cin.ignore(); // Ignore leftover newline character

        if (user_type == "seller") {
            if (services_choice == 1) {
                // Create Service
                std::string service_name, price, capacity, working_hours, service_type, loyalty_requirement, loyalty_discount;
                std::cout << "Enter Service Name: ";
                std::getline(std::cin, service_name);
                std::cout << "Enter Price: ";
                std::getline(std::cin, price);
                std::cout << "Enter Capacity: ";
                std::getline(std::cin, capacity);
                std::cout << "Enter Working Hours: ";
                std::getline(std::cin, working_hours);
                std::cout << "Enter Service Type: ";
                std::getline(std::cin, service_type);
                std::cout << "Enter Loyalty Requirement: ";
                std::getline(std::cin, loyalty_requirement);
                std::cout << "Enter Loyalty Discount: ";
                std::getline(std::cin, loyalty_discount);

                std::string service_data = "service_name=" + service_name + "&price=" + price + "&capacity=" + capacity +
                                           "&working_hours=" + working_hours + "&service_type=" + service_type +
                                           "&loyalty_requirement=" + loyalty_requirement + "&loyalty_discount=" + loyalty_discount;
                std::string response_data;

                long response_code = send_request(host, port, "/create_service", service_data, response_data, &token, false); // POST request
                if (response_code == 200) {
                    std::cout << "Service created successfully.\n";
                } else {
                    std::cerr << "Failed to create service. Response Code: " << response_code << "\n";
                }
            } else if (services_choice == 2) {
                // View My Services
                std::string response_data;
                send_request(host, port, "/my_services", "", response_data, &token); // GET request
                std::cout << "My Services: " << response_data << std::endl;
            } else if (services_choice == 3) {
                // Delete Service
                std::string service_id;
                std::cout << "Enter Service ID to Delete: ";
                std::getline(std::cin, service_id);

                std::string delete_data = "service_id=" + service_id;
                std::string response_data;

                long response_code = send_request(host, port, "/delete_service", delete_data, response_data, &token, false); // POST request
                if (response_code == 200) {
                    std::cout << "Service deleted successfully.\n";
                } else {
                    std::cerr << "Failed to delete service. Response Code: " << response_code << "\n";
                }
            } else if (services_choice == 4) {
                // Update Service
                std::string service_id;
                std::cout << "Enter Service ID to Update: ";
                std::getline(std::cin, service_id);

                std::string field, new_value;
                std::cout << "Enter Field to Update (e.g., price, capacity): ";
                std::getline(std::cin, field);
                std::cout << "Enter New Value: ";
                std::getline(std::cin, new_value);

                std::string update_data = "service_id=" + service_id + "&field=" + field + "&new_value=" + new_value;
                std::string response_data;

                long response_code = send_request(host, port, "/update_service", update_data, response_data, &token, false); // POST request
                if (response_code == 200) {
                    std::cout << "Service updated successfully.\n";
                } else {
                    std::cerr << "Failed to update service. Response Code: " << response_code << "\n";
                }
            } else if (services_choice == 5) {
                // Back
                break; // Return to dashboard menu
            } else {
                std::cerr << "Invalid choice. Please try again.\n";
            }
        } else if (user_type == "buyer") {
            if (services_choice == 1) {
                // View All Services
                std::string response_data;
                send_request(host, port, "/all_services", "", response_data); // GET request
                std::cout << "All Services: " << response_data << std::endl;
            } else if (services_choice == 2) {
                // Back
                break; // Return to dashboard menu
            } else {
                std::cerr << "Invalid choice. Please try again.\n";
            }
        }
    }
}

// Function to show the dashboard menu
void show_dashboard(const std::string& host, int port, const std::string& token, const std::string& user_type) {
    while (true) {
        std::cout << "\nDashboard Menu:\n";
        std::cout << "1. Orders\n";
        std::cout << "2. Services\n";
        std::cout << "3. Loyalty\n";
        std::cout << "4. Profile\n";
        std::cout << "5. Logout\n";

        int dashboard_choice;
        std::cin >> dashboard_choice;
        std::cin.ignore(); // Ignore leftover newline character

        if (dashboard_choice == 1)  {
            if (user_type == "seller") handle_seller_orders_menu(host, port, token);
            else if (user_type == "buyer") handle_buyer_orders_menu(host, port, token);
        } else if (dashboard_choice == 2) {
            handle_services_menu(host, port, token, user_type);
        } else if (dashboard_choice == 3) {
            // Handle Loyalty menu
            if (user_type == "buyer") {
                std::string response_data;
                send_request(host, port, "/loyalty/buyers", "", response_data, &token); // GET request
                std::cout << "Loyalty Information: " << response_data << std::endl;
            } else if (user_type == "seller") {
                std::string response_data;
                send_request(host, port, "/loyalty/sellers", "", response_data, &token); // GET request
                std::cout << "Loyalty Information: " << response_data << std::endl;
            }
        } else if (dashboard_choice == 4) {
            // Handle Profile menu
             handle_profile_menu(host, port, token, user_type);
        } else if (dashboard_choice == 5) {
            // Handle Logout
            std::string response_data;
            long response_code = send_request(host, port, "/logout", "", response_data, &token, false); // POST request for logout

            if (response_code == 200) {
                std::cout << "Logged out successfully.\n";
                break; // Exit the dashboard menu after successful logout
            } else {
                std::cerr << "Failed to log out." << "\n";
            }
        } else {
            std::cerr << "Invalid choice. Please try again.\n";
        }
    }
}


// Function to show the main menu
void show_main_menu(const std::string& host, int port) {
    while (true) {
        std::cout << "\nMain Menu:\n";
        std::cout << "1. Login\n";
        std::cout << "2. Register\n";
        std::cout << "3. Exit\n";

        int main_choice;
        std::cin >> main_choice;
        std::cin.ignore(); // Ignore leftover newline character

        if (main_choice == 1) {
            std::string username, password;
            std::cout << "Enter Username: ";
            std::getline(std::cin, username);
            std::cout << "Enter Password: ";
            std::getline(std::cin, password);

            auto [token, user_type] = send_login_request(host, port, username, password);
            if (!token.empty() && !user_type.empty()) {
                std::cout << "Login successful.\n";
                show_dashboard(host, port, token, user_type);
            } else {
                std::cerr << "Login failed. Please check your credentials.\n";
            }
        } else if (main_choice == 2) {
            std::string username, password, email;
            std::cout << "Enter Username: ";
            std::getline(std::cin, username);
            std::cout << "Enter Password: ";
            std::getline(std::cin, password);
            std::cout << "Enter Email Address: ";
            std::getline(std::cin, email);


            send_registration_request(host, port, username, password, email);
        } else if (main_choice == 3) {
            break; // Exit the main menu
        } else {
            std::cerr << "Invalid choice. Please try again.\n";
        }
    }
}

int main() {
    std::string host = "localhost"; // Change to your server host
    int port = 8080; // Change to your server port

    show_main_menu(host, port);

    return 0;
}
