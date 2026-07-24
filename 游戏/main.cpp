#include "Game.h"

// 控制台子系统入口。Game 封装了 Win32 窗口、消息循环和所有游戏资源，
// 因此入口只负责创建控制器并把退出码传回操作系统。
int main()
{
    Game game;
    return game.Run();
}
