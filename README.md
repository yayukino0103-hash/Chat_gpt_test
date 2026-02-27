# 五子棋服务器（Linux / C++）

这是一个纯 C++ 实现的五子棋 TCP 服务器，支持：

- 多客户端连接（socket + `select`）
- 用户登录管理
- 房间管理（创建、加入）
- 对局数据转发（落子消息）
- 好友系统（添加好友、好友列表、在线状态通知）
- Elo 排名系统（每局结束更新积分、排行榜前100）
- 简单 JSON 协议通信（每行一条 JSON）

## 协议示例

### 1) 登录
客户端请求：
```json
{"type":"login","username":"alice","password":"123"}
```
服务端响应：
```json
{"type":"login_result","ok":true,"username":"alice"}
```

### 2) 创建房间
客户端请求：
```json
{"type":"create_room","room_id":"room_1"}
```

### 3) 加入房间
客户端请求：
```json
{"type":"join_room","room_id":"room_1"}
```

### 4) 落子（服务器转发给房间内所有成员）
客户端请求：
```json
{"type":"move","row":7,"col":7,"player":1}
```
服务端转发：
```json
{"type":"move","room_id":"room_1","username":"alice","row":7,"col":7,"player":1}
```

### 5) 添加好友
客户端请求：
```json
{"type":"add_friend","friend_username":"bob"}
```
服务端响应：
```json
{"type":"add_friend_result","ok":true,"friend_username":"bob","online":true}
```

### 6) 获取好友列表
客户端请求：
```json
{"type":"list_friends"}
```
服务端响应（逗号分隔字符串）：
```json
{"type":"friend_list","friends":"bob,carol","online_friends":"bob","count":2,"online_count":1}
```

### 7) 在线状态通知
当好友上线/离线时，服务器推送：
```json
{"type":"friend_status","username":"bob","online":true}
{"type":"friend_status","username":"bob","online":false}
```


### 8) 对局结束（更新 Elo 积分）
客户端请求：
```json
{"type":"game_over","winner":"alice","loser":"bob"}
```
服务端响应：
```json
{"type":"game_over_result","ok":true,"winner":"alice","loser":"bob","winner_old":1200,"winner_new":1216,"loser_old":1200,"loser_new":1184}
```

### 9) 排行榜（前100）
客户端请求：
```json
{"type":"leaderboard"}
```
服务端响应（示例）：
```json
{"type":"leaderboard_result","count":3,"p1":"alice:1216","p2":"carol:1208","p3":"bob:1184"}
```

## 项目结构

- `include/gomoku_server.h`：服务器核心类定义
- `src/gomoku_server.cpp`：连接管理、登录、房间、转发、好友逻辑
- `include/simple_json.h`：简单 JSON 工具声明
- `src/simple_json.cpp`：简单 JSON 解析/构造实现
- `main.cpp`：程序入口
- `Makefile`：Linux 构建脚本

## 构建与运行

```bash
make
./gomoku_server 9000
```

不传端口默认 `9000`。

## 通信约定

- 文本协议，UTF-8，每条消息以 `\n` 结尾。
- 服务器当前支持平面 JSON 对象（键值对），适合登录/房间/落子/好友这类消息。
