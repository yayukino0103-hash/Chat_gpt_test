#include <iostream>
#include <vector>
#include <conio.h>
#include <windows.h>
#include <ctime>
#include <cstdlib>
#include <string>

using namespace std;

const int WIDTH = 20;
const int HEIGHT = 10;
const int FRAME_MS = 90;

enum Direction {
    UP,
    DOWN,
    LEFT,
    RIGHT
};

struct Point {
    int x;
    int y;
};

vector<Point> snake;
Point food;
Direction dir;
bool running;
int score;
HANDLE hConsole;

bool hitWall(const Point& p) {
    return p.x < 0 || p.x >= WIDTH || p.y < 0 || p.y >= HEIGHT;
}

bool hitSelf(const Point& head, bool willGrow) {
    size_t collisionLimit = willGrow ? snake.size() : snake.size() - 1;
    for (size_t i = 0; i < collisionLimit; ++i) {
        if (snake[i].x == head.x && snake[i].y == head.y) {
            return true;
        }
    }
    return false;
}

bool isOpposite(Direction a, Direction b) {
    return (a == UP && b == DOWN) ||
           (a == DOWN && b == UP) ||
           (a == LEFT && b == RIGHT) ||
           (a == RIGHT && b == LEFT);
}

bool isOnSnake(const Point& p) {
    for (size_t i = 0; i < snake.size(); ++i) {
        if (snake[i].x == p.x && snake[i].y == p.y) {
            return true;
        }
    }
    return false;
}

void setupConsole() {
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    system("cls");
}

void moveCursorToTopLeft() {
    COORD pos = {0, 0};
    SetConsoleCursorPosition(hConsole, pos);
}

void spawnFood() {
    if (snake.size() >= static_cast<size_t>(WIDTH * HEIGHT)) {
        running = false;
        return;
    }

    do {
        food.x = rand() % WIDTH;
        food.y = rand() % HEIGHT;
    } while (isOnSnake(food));
}

void init() {
    snake.clear();
    snake.push_back({WIDTH / 2, HEIGHT / 2});
    snake.push_back({WIDTH / 2 - 1, HEIGHT / 2});
    snake.push_back({WIDTH / 2 - 2, HEIGHT / 2});

    dir = RIGHT;
    running = true;
    score = 0;
    spawnFood();
}

void input() {
    if (_kbhit()) {
        char key = _getch();
        Direction nextDir = dir;

        if (key == 'w' || key == 'W') nextDir = UP;
        else if (key == 's' || key == 'S') nextDir = DOWN;
        else if (key == 'a' || key == 'A') nextDir = LEFT;
        else if (key == 'd' || key == 'D') nextDir = RIGHT;
        else if (key == 'q' || key == 'Q') {
            running = false;
            return;
        }

        if (!isOpposite(dir, nextDir)) {
            dir = nextDir;
        }
    }
}

void update() {
    Point newHead = snake.front();

    if (dir == UP) newHead.y--;
    else if (dir == DOWN) newHead.y++;
    else if (dir == LEFT) newHead.x--;
    else if (dir == RIGHT) newHead.x++;

    if (hitWall(newHead)) {
        running = false;
        return;
    }

    bool willGrow = (newHead.x == food.x && newHead.y == food.y);
    if (hitSelf(newHead, willGrow)) {
        running = false;
        return;
    }

    snake.insert(snake.begin(), newHead);

    if (willGrow) {
        score++;
        spawnFood();
    } else {
        snake.pop_back();
    }
}

void render() {
    moveCursorToTopLeft();

    string frame;
    frame.reserve((HEIGHT + 4) * (WIDTH + 4));

    for (int y = 0; y < HEIGHT + 2; ++y) {
        for (int x = 0; x < WIDTH + 2; ++x) {
            if (y == 0 || y == HEIGHT + 1 || x == 0 || x == WIDTH + 1) {
                frame += '#';
                continue;
            }

            if (food.x == x - 1 && food.y == y - 1) {
                frame += '*';
                continue;
            }

            bool isSnake = false;
            for (size_t i = 0; i < snake.size(); ++i) {
                if (snake[i].x == x - 1 && snake[i].y == y - 1) {
                    frame += (i == 0 ? 'O' : 'o');
                    isSnake = true;
                    break;
                }
            }

            if (!isSnake) frame += ' ';
        }
        frame += '\n';
    }

    frame += "得分: " + to_string(score) + "    WASD 控制方向，Q 退出\n";
    cout << frame;
}

int main() {
    srand(static_cast<unsigned int>(time(0)));
    setupConsole();
    init();

    while (running) {
        input();
        update();
        render();
        Sleep(FRAME_MS);
    }

    cout << "Game Over! Final Score: " << score << endl;
    return 0;
}
