kompajliranje klijenta
g++ client.cpp -o client -I/opt/homebrew/opt/boost/include -L/opt/homebrew/opt/boost/lib -lboost_system -lcurl -std=c++17

kompajliranje regionalnog servera
g++ regional_server.cpp -o regional_server -I/opt/homebrew/opt/boost/include -L/opt/homebrew/opt/boost/lib -lboost_system -L/opt/homebrew/opt/sqlite/lib -lsqlite3 -std=c++11

kompajliranje centralnog servera
g++ central_server.cpp -o central_server -I/opt/homebrew/opt/boost/include -L/opt/homebrew/opt/boost/lib -lboost_system -L/opt/homebrew/opt/sqlite/lib -lsqlite3 -std=c++11


------

# argumenti za pokretanje regionalnih servera:
# port za klijente, 
# adresa centralnog servera, 
# port centralnog servera, 
# id regionalnog servera, 
# baza podataka, 
# interval za pokretanej sinkronizacija u minutama

# pokretanje Regionalnog Servera 1 i spajanje na centralni port 8081
./regional_server 8080 127.0.0.1 8081 regional_server_1 baza1.db 5

# pokretanje Regionalnog Servera 2 i spajanje na centralni port 8082
./regional_server 8079 127.0.0.1 8082 regional_server_2 baza2.db 4

# argumenti za pokretanje centralnog servera sa portovima izmedju 8081 i 8082
./central_server 8081 8082 central_baza.db

pokretanje klijenta i spajanje na port regionalnog servera 1
./client 127.0.0.1 8080