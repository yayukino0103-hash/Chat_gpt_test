#ifndef GOMOKU_SERVER_H
#define GOMOKU_SERVER_H

#include <string>
#include <unordered_map>
#include <unordered_set>

struct ClientSession {
    int fd = -1;
    std::string recvBuffer;
    std::string username;
    bool loggedIn = false;
    std::string roomId;
};

struct Room {
    std::string roomId;
    std::unordered_set<int> members;
};

class GomokuServer {
public:
    explicit GomokuServer(int port);
    ~GomokuServer();

    bool start();
    void run();

private:
    int port_;
    int listenFd_ = -1;

    std::unordered_map<int, ClientSession> clients_;
    std::unordered_map<std::string, int> userToFd_;
    std::unordered_map<std::string, Room> rooms_;

    std::unordered_set<std::string> knownUsers_;
    std::unordered_map<std::string, std::unordered_set<std::string>> friends_;
    std::unordered_map<std::string, int> ratings_;

    void acceptClient();
    void readFromClient(int fd);
    void disconnectClient(int fd);

    void processLine(int fd, const std::string& line);
    void handleLogin(int fd, const std::unordered_map<std::string, std::string>& msg);
    void handleCreateRoom(int fd, const std::unordered_map<std::string, std::string>& msg);
    void handleJoinRoom(int fd, const std::unordered_map<std::string, std::string>& msg);
    void handleMove(int fd, const std::unordered_map<std::string, std::string>& msg);
    void handleAddFriend(int fd, const std::unordered_map<std::string, std::string>& msg);
    void handleListFriends(int fd);
    void handleGameOver(int fd, const std::unordered_map<std::string, std::string>& msg);
    void handleLeaderboard(int fd);

    void sendToClient(int fd, const std::string& message);
    void sendError(int fd, const std::string& reason);
    void broadcastRoom(const std::string& roomId, const std::string& message, int exceptFd = -1);
    void notifyFriendsOnlineStatus(const std::string& username, bool online);

    bool isLoggedIn(int fd) const;
};

#endif // GOMOKU_SERVER_H
