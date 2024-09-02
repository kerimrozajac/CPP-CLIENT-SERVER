kompajliranje servera
g++ server.cpp -o server -I/opt/homebrew/opt/boost/include -L/opt/homebrew/opt/boost/lib -lboost_system -L/opt/homebrew/opt/sqlite/lib -lsqlite3 -std=c++11

pokretanje servera
./server 8080


kompajliranje klijenta
g++ client.cpp -o client -I/opt/homebrew/opt/boost/include -L/opt/homebrew/opt/boost/lib -lboost_system -lcurl -std=c++17

pokretanje klijenta
./client 127.0.0.1 8080