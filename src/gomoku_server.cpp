#include "gomoku_server.h"

#include "simple_json.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
constexpr int kMaxBuffer = 4096;
constexpr int kDefaultRating = 1200;
constexpr double kEloKFactor = 32.0;

double expectedScore(int ra, int rb) {
    return 1.0 / (1.0 + std::pow(10.0, (static_cast<double>(rb - ra) / 400.0)));
}

int calculateNewRating(int oldRating, double actualScore, double expected) {
    return static_cast<int>(std::lround(oldRating + kEloKFactor * (actualScore - expected)));
}

std::string joinByComma(const std::unordered_set<std::string>& items) {
    std::string out;
    bool first = true;
    for (const auto& item : items) {
        if (!first) {
            out += ',';
        }
        first = false;
        out += item;
    }
    return out;
}

std::string joinOnlineFriends(const std::unordered_set<std::string>& items,
                              const std::unordered_map<std::string, int>& onlineUsers) {
    std::string out;
    bool first = true;
    for (const auto& item : items) {
        if (onlineUsers.count(item) == 0) {
            continue;
        }
        if (!first) {
            out += ',';
        }
        first = false;
        out += item;
    }
    return out;
}


std::size_t countOnlineFriends(const std::unordered_set<std::string>& items,
                               const std::unordered_map<std::string, int>& onlineUsers) {
    std::size_t cnt = 0;
    for (const auto& item : items) {
        if (onlineUsers.count(item) > 0) {
            ++cnt;
        }
    }
    return cnt;
}
} // namespace

GomokuServer::GomokuServer(int port) : port_(port) {}

GomokuServer::~GomokuServer() {
    for (const auto& [fd, _] : clients_) {
        close(fd);
    }
    if (listenFd_ >= 0) {
        close(listenFd_);
    }
}

bool GomokuServer::start() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << '\n';
        return false;
    }

    int reuse = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << std::strerror(errno) << '\n';
        return false;
    }

    if (listen(listenFd_, 16) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << '\n';
        return false;
    }

    std::cout << "Gomoku server started at 0.0.0.0:" << port_ << '\n';
    return true;
}

void GomokuServer::run() {
    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listenFd_, &readfds);
        int maxFd = listenFd_;

        for (const auto& [fd, _] : clients_) {
            FD_SET(fd, &readfds);
            if (fd > maxFd) {
                maxFd = fd;
            }
        }

        const int ready = select(maxFd + 1, &readfds, nullptr, nullptr, nullptr);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "select() failed: " << std::strerror(errno) << '\n';
            break;
        }

        if (FD_ISSET(listenFd_, &readfds)) {
            acceptClient();
        }

        std::vector<int> fds;
        fds.reserve(clients_.size());
        for (const auto& [fd, _] : clients_) {
            if (FD_ISSET(fd, &readfds)) {
                fds.push_back(fd);
            }
        }

        for (int fd : fds) {
            readFromClient(fd);
        }
    }
}

void GomokuServer::acceptClient() {
    sockaddr_in clientAddr{};
    socklen_t len = sizeof(clientAddr);
    const int clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &len);
    if (clientFd < 0) {
        return;
    }

    ClientSession session;
    session.fd = clientFd;
    clients_[clientFd] = session;
    sendToClient(clientFd, buildJsonObject({{"type", "welcome"}, {"message", "connected"}}));
    std::cout << "Client connected: fd=" << clientFd << '\n';
}

void GomokuServer::readFromClient(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }

    char buf[kMaxBuffer];
    const ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
        disconnectClient(fd);
        return;
    }

    it->second.recvBuffer.append(buf, static_cast<std::size_t>(n));

    while (true) {
        const std::size_t pos = it->second.recvBuffer.find('\n');
        if (pos == std::string::npos) {
            break;
        }

        std::string line = it->second.recvBuffer.substr(0, pos);
        it->second.recvBuffer.erase(0, pos + 1);

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        processLine(fd, line);
    }
}

void GomokuServer::disconnectClient(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }

    if (it->second.loggedIn) {
        const std::string username = it->second.username;
        userToFd_.erase(username);
        notifyFriendsOnlineStatus(username, false);
    }

    if (!it->second.roomId.empty()) {
        const std::string roomId = it->second.roomId;
        auto rit = rooms_.find(roomId);
        if (rit != rooms_.end()) {
            rit->second.members.erase(fd);
            broadcastRoom(roomId,
                          buildJsonObject({{"type", "player_left"},
                                           {"username", it->second.username},
                                           {"room_id", roomId}}),
                          fd);
            if (rit->second.members.empty()) {
                rooms_.erase(rit);
            }
        }
    }

    close(fd);
    clients_.erase(it);
    std::cout << "Client disconnected: fd=" << fd << '\n';
}

void GomokuServer::processLine(int fd, const std::string& line) {
    std::unordered_map<std::string, std::string> msg;
    if (!parseJsonObject(line, msg)) {
        sendError(fd, "invalid_json");
        return;
    }

    const auto typeIt = msg.find("type");
    if (typeIt == msg.end()) {
        sendError(fd, "missing_type");
        return;
    }

    const std::string& type = typeIt->second;
    if (type == "login") {
        handleLogin(fd, msg);
    } else if (type == "create_room") {
        handleCreateRoom(fd, msg);
    } else if (type == "join_room") {
        handleJoinRoom(fd, msg);
    } else if (type == "move") {
        handleMove(fd, msg);
    } else if (type == "add_friend") {
        handleAddFriend(fd, msg);
    } else if (type == "list_friends") {
        handleListFriends(fd);
    } else if (type == "game_over") {
        handleGameOver(fd, msg);
    } else if (type == "leaderboard") {
        handleLeaderboard(fd);
    } else {
        sendError(fd, "unknown_type");
    }
}

void GomokuServer::handleLogin(int fd, const std::unordered_map<std::string, std::string>& msg) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }

    const auto userIt = msg.find("username");
    const auto passIt = msg.find("password");
    if (userIt == msg.end() || passIt == msg.end()) {
        sendError(fd, "missing_login_fields");
        return;
    }

    const std::string username = userIt->second;
    if (username.empty()) {
        sendError(fd, "empty_username");
        return;
    }

    if (userToFd_.count(username) > 0 && userToFd_[username] != fd) {
        sendToClient(fd,
                     buildJsonObject({{"type", "login_result"}, {"ok", "false", false}, {"reason", "user_exists"}}));
        return;
    }

    const bool wasOnline = isLoggedIn(fd);
    if (wasOnline && it->second.username != username) {
        userToFd_.erase(it->second.username);
    }

    it->second.username = username;
    it->second.loggedIn = true;
    userToFd_[username] = fd;
    knownUsers_.insert(username);
    friends_[username];
    if (ratings_.count(username) == 0) {
        ratings_[username] = kDefaultRating;
    }

    sendToClient(fd,
                 buildJsonObject({{"type", "login_result"}, {"ok", "true", false}, {"username", username}}));

    notifyFriendsOnlineStatus(username, true);
}

void GomokuServer::handleCreateRoom(int fd, const std::unordered_map<std::string, std::string>& msg) {
    if (!isLoggedIn(fd)) {
        sendError(fd, "not_logged_in");
        return;
    }

    const auto ridIt = msg.find("room_id");
    if (ridIt == msg.end() || ridIt->second.empty()) {
        sendError(fd, "missing_room_id");
        return;
    }

    const std::string roomId = ridIt->second;
    if (rooms_.count(roomId) > 0) {
        sendToClient(fd,
                     buildJsonObject({{"type", "create_room_result"}, {"ok", "false", false}, {"reason", "room_exists"}}));
        return;
    }

    auto& room = rooms_[roomId];
    room.roomId = roomId;
    room.members.insert(fd);

    clients_[fd].roomId = roomId;

    sendToClient(fd,
                 buildJsonObject({{"type", "create_room_result"}, {"ok", "true", false}, {"room_id", roomId}}));
}

void GomokuServer::handleJoinRoom(int fd, const std::unordered_map<std::string, std::string>& msg) {
    if (!isLoggedIn(fd)) {
        sendError(fd, "not_logged_in");
        return;
    }

    const auto ridIt = msg.find("room_id");
    if (ridIt == msg.end() || ridIt->second.empty()) {
        sendError(fd, "missing_room_id");
        return;
    }

    const std::string roomId = ridIt->second;
    auto rit = rooms_.find(roomId);
    if (rit == rooms_.end()) {
        sendToClient(fd,
                     buildJsonObject({{"type", "join_room_result"}, {"ok", "false", false}, {"reason", "room_not_found"}}));
        return;
    }

    if (rit->second.members.size() >= 2) {
        sendToClient(fd,
                     buildJsonObject({{"type", "join_room_result"}, {"ok", "false", false}, {"reason", "room_full"}}));
        return;
    }

    rit->second.members.insert(fd);
    clients_[fd].roomId = roomId;

    sendToClient(fd,
                 buildJsonObject({{"type", "join_room_result"}, {"ok", "true", false}, {"room_id", roomId}}));

    broadcastRoom(roomId,
                  buildJsonObject({{"type", "player_joined"},
                                   {"room_id", roomId},
                                   {"username", clients_[fd].username}}),
                  fd);
}

void GomokuServer::handleMove(int fd, const std::unordered_map<std::string, std::string>& msg) {
    if (!isLoggedIn(fd)) {
        sendError(fd, "not_logged_in");
        return;
    }

    const std::string roomId = clients_[fd].roomId;
    if (roomId.empty() || rooms_.count(roomId) == 0) {
        sendError(fd, "not_in_room");
        return;
    }

    const auto rowIt = msg.find("row");
    const auto colIt = msg.find("col");
    const auto playerIt = msg.find("player");
    if (rowIt == msg.end() || colIt == msg.end() || playerIt == msg.end()) {
        sendError(fd, "missing_move_fields");
        return;
    }

    std::string packet = buildJsonObject({{"type", "move"},
                                          {"room_id", roomId},
                                          {"username", clients_[fd].username},
                                          {"row", rowIt->second, false},
                                          {"col", colIt->second, false},
                                          {"player", playerIt->second, false}});

    broadcastRoom(roomId, packet, -1);
}

void GomokuServer::handleAddFriend(int fd, const std::unordered_map<std::string, std::string>& msg) {
    if (!isLoggedIn(fd)) {
        sendError(fd, "not_logged_in");
        return;
    }

    const auto targetIt = msg.find("friend_username");
    if (targetIt == msg.end() || targetIt->second.empty()) {
        sendError(fd, "missing_friend_username");
        return;
    }

    const std::string me = clients_[fd].username;
    const std::string target = targetIt->second;

    if (target == me) {
        sendToClient(fd,
                     buildJsonObject({{"type", "add_friend_result"}, {"ok", "false", false}, {"reason", "cannot_add_self"}}));
        return;
    }

    if (knownUsers_.count(target) == 0) {
        sendToClient(fd,
                     buildJsonObject({{"type", "add_friend_result"}, {"ok", "false", false}, {"reason", "user_not_found"}}));
        return;
    }

    friends_[me].insert(target);
    friends_[target].insert(me);

    sendToClient(fd,
                 buildJsonObject({{"type", "add_friend_result"},
                                  {"ok", "true", false},
                                  {"friend_username", target},
                                  {"online", userToFd_.count(target) ? "true" : "false", false}}));

    if (userToFd_.count(target) > 0) {
        sendToClient(userToFd_[target],
                     buildJsonObject({{"type", "friend_added"}, {"username", me}}));
    }
}

void GomokuServer::handleListFriends(int fd) {
    if (!isLoggedIn(fd)) {
        sendError(fd, "not_logged_in");
        return;
    }

    const std::string me = clients_[fd].username;
    const auto it = friends_.find(me);
    if (it == friends_.end()) {
        sendToClient(fd,
                     buildJsonObject({{"type", "friend_list"},
                                      {"friends", ""},
                                      {"online_friends", ""},
                                      {"count", "0", false},
                                      {"online_count", "0", false}}));
        return;
    }

    const std::string allFriends = joinByComma(it->second);
    const std::string onlineFriends = joinOnlineFriends(it->second, userToFd_);

    sendToClient(fd,
                 buildJsonObject({{"type", "friend_list"},
                                  {"friends", allFriends},
                                  {"online_friends", onlineFriends},
                                  {"count", std::to_string(it->second.size()), false},
                                  {"online_count", std::to_string(countOnlineFriends(it->second, userToFd_)), false}}));
}

void GomokuServer::handleGameOver(int fd, const std::unordered_map<std::string, std::string>& msg) {
    if (!isLoggedIn(fd)) {
        sendError(fd, "not_logged_in");
        return;
    }

    const auto winnerIt = msg.find("winner");
    const auto loserIt = msg.find("loser");
    if (winnerIt == msg.end() || loserIt == msg.end() || winnerIt->second.empty() || loserIt->second.empty()) {
        sendError(fd, "missing_game_over_fields");
        return;
    }

    const std::string winner = winnerIt->second;
    const std::string loser = loserIt->second;
    if (winner == loser) {
        sendError(fd, "invalid_game_over_players");
        return;
    }

    if (knownUsers_.count(winner) == 0 || knownUsers_.count(loser) == 0) {
        sendError(fd, "unknown_player");
        return;
    }

    if (ratings_.count(winner) == 0) {
        ratings_[winner] = kDefaultRating;
    }
    if (ratings_.count(loser) == 0) {
        ratings_[loser] = kDefaultRating;
    }

    const int winnerOld = ratings_[winner];
    const int loserOld = ratings_[loser];

    const double winnerExpected = expectedScore(winnerOld, loserOld);
    const double loserExpected = expectedScore(loserOld, winnerOld);

    const int winnerNew = calculateNewRating(winnerOld, 1.0, winnerExpected);
    const int loserNew = calculateNewRating(loserOld, 0.0, loserExpected);

    ratings_[winner] = winnerNew;
    ratings_[loser] = loserNew;

    const std::string packet = buildJsonObject({{"type", "game_over_result"},
                                                {"ok", "true", false},
                                                {"winner", winner},
                                                {"loser", loser},
                                                {"winner_old", std::to_string(winnerOld), false},
                                                {"winner_new", std::to_string(winnerNew), false},
                                                {"loser_old", std::to_string(loserOld), false},
                                                {"loser_new", std::to_string(loserNew), false}});

    sendToClient(fd, packet);
    auto wit = userToFd_.find(winner);
    if (wit != userToFd_.end() && wit->second != fd) {
        sendToClient(wit->second, packet);
    }
    auto lit = userToFd_.find(loser);
    if (lit != userToFd_.end() && lit->second != fd && lit->second != (wit != userToFd_.end() ? wit->second : -1)) {
        sendToClient(lit->second, packet);
    }
}

void GomokuServer::handleLeaderboard(int fd) {
    if (!isLoggedIn(fd)) {
        sendError(fd, "not_logged_in");
        return;
    }

    std::vector<std::pair<std::string, int>> ranking;
    ranking.reserve(ratings_.size());
    for (const auto& [name, score] : ratings_) {
        ranking.emplace_back(name, score);
    }

    std::sort(ranking.begin(), ranking.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) {
            return a.second > b.second;
        }
        return a.first < b.first;
    });

    const std::size_t limit = std::min<std::size_t>(100, ranking.size());
    std::vector<JsonField> fields;
    fields.push_back({"type", "leaderboard_result", true});
    fields.push_back({"count", std::to_string(limit), false});

    for (std::size_t i = 0; i < limit; ++i) {
        fields.push_back({"p" + std::to_string(i + 1), ranking[i].first + ":" + std::to_string(ranking[i].second), true});
    }

    sendToClient(fd, buildJsonObject(fields));
}

void GomokuServer::sendToClient(int fd, const std::string& message) {
    const std::string data = message + "\n";
    send(fd, data.c_str(), data.size(), 0);
}

void GomokuServer::sendError(int fd, const std::string& reason) {
    sendToClient(fd, buildJsonObject({{"type", "error"}, {"reason", reason}}));
}

void GomokuServer::broadcastRoom(const std::string& roomId, const std::string& message, int exceptFd) {
    auto rit = rooms_.find(roomId);
    if (rit == rooms_.end()) {
        return;
    }

    for (int memberFd : rit->second.members) {
        if (memberFd == exceptFd) {
            continue;
        }
        sendToClient(memberFd, message);
    }
}

void GomokuServer::notifyFriendsOnlineStatus(const std::string& username, bool online) {
    auto fit = friends_.find(username);
    if (fit == friends_.end()) {
        return;
    }

    for (const auto& friendName : fit->second) {
        auto uit = userToFd_.find(friendName);
        if (uit == userToFd_.end()) {
            continue;
        }
        sendToClient(uit->second,
                     buildJsonObject({{"type", "friend_status"},
                                      {"username", username},
                                      {"online", online ? "true" : "false", false}}));
    }
}

bool GomokuServer::isLoggedIn(int fd) const {
    auto it = clients_.find(fd);
    return it != clients_.end() && it->second.loggedIn;
}
