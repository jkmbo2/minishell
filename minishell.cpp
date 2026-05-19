#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h> // STDFILENO标准输入的fd
#include <sys/wait.h>
#include <sys/types.h>
#include <climits>
#include <termios.h> // for tcgetattr/tcsetattr 即terminal get/set attributes，
// 用来将终端从默认的行缓冲切换到字符缓冲，行缓冲就是只有当用户输入回车时才会刷新缓冲区

using namespace std;

vector<string> history;
int his_index = 0;

struct termios orig_termios;

void enableRawMode()
{
	tcgetattr(STDIN_FILENO, &orig_termios);
	struct termios raw = orig_termios;
	// 关回显，c_lflag是local flag的意思,ICANON是规范模式，即行缓冲模式，
	// 为什么要用位处理是因为c_lflag一般是一个32位或者64位的整数，而像ECHO和ICANON这种开关就是其中一位，
	// 也就是说对于ECHO和ICANON这种开关，就是一个所有位数上只有特定位为1的整数
	// 关回显是因为如果不关的话，输入的非正常字符如退格键，空格键等等都会显示出来，所以要关掉，然后自己来控制屏幕输出
	raw.c_lflag &= ~(ECHO | ICANON);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);// 中间的参数指的是什么时候应用新设置
}

void disableRawMode()
{
	
}

// 拆分用户的输入并执行命令
void divideAndExecute(const vector<string>& args)
{
	vector<char*> c_args;
	for (const auto& s : args)
	{
		c_args.push_back(const_cast<char*>(s.c_str()));
	}
	c_args.push_back(nullptr);

	execvp(c_args[0], c_args.data());
	perror("execvp failed");
	_exit(EXIT_FAILURE);
}

// 创建管道
void createpipe(vector<string>& left, vector<string>& right)
{
	int fd[2];
	pipe(fd);// fd, file descriptor文件描述符，传入pipe函数后被初始化

	pid_t pid1 = fork();
	

	if (pid1 == 0)
	{
		dup2(fd[1], STDOUT_FILENO);
		close(fd[0]);
		close(fd[1]);
		divideAndExecute(left);// 这里pid1进程执行完命令就死掉了
	}
	
	pid_t pid2 = fork();
	if (pid2 == 0)
	{
		dup2(fd[0], STDIN_FILENO);
		close(fd[1]);
		close(fd[0]);
		divideAndExecute(right);
	}

	close(fd[0]);
	close(fd[1]);
	waitpid(pid1, nullptr, 0);
	waitpid(pid2, nullptr, 0);
}

vector<string> eval(string& input)
{
	stringstream ss(input);
	string token;
	vector<string> command;

	while (ss >> token)
	{
		command.push_back(token);
	}

	return command;

}

void execute(const vector<string>& args)
{
	if (args.empty()) return;

	if (args[0] == "cd")
	{
		if (args.size() == 1)
		{
			perror("Please input path");
			return;
		}
		chdir(args[1].c_str());
		return;
	}
	else if (args[0] == "exit")
	{
		exit(EXIT_SUCCESS);
	}

	vector<string> left;
	vector<string> right;
	int foundPipe = 0;
	for (auto& s : args)
	{
		if (s == "|")
		{
			foundPipe = 1;
			continue;
		}
		if (!foundPipe)
		{
			left.push_back(s);
		}
		else
		{
			right.push_back(s);
		}
	}

	if (foundPipe)
	{
		createpipe(left, right);
		return;
	}

	pid_t pid = fork();

	if (pid < 0)
	{
		perror("fork failed");
		return;
	}

	if (pid == 0)
	{
		divideAndExecute(args);
	}

	else
	{
		int status;
		waitpid(pid, &status, 0);
	}
}

void read_user_input()
{
	string input;
	char buffer[PATH_MAX];

	cout << "sofei@" << getcwd(buffer, sizeof buffer);
	cout << ">>";
	while (getline(cin, input))
	{
		history.push_back(input);
		execute(eval(input));
		cout << "\nsofei@" << getcwd(buffer, sizeof buffer);
		cout << ">>";
	}
}


int main()
{
	read_user_input();

}

