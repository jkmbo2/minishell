#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h> // STDFILENO标准输入的fd, read()
#include <sys/wait.h>
#include <sys/types.h>
#include <climits>
#include <termios.h> // for tcgetattr/tcsetattr 即terminal get/set attributes，
// 用来将终端从默认的行缓冲切换到字符缓冲，行缓冲就是只有当用户输入回车时才会刷新缓冲区

using namespace std;

vector<string> history;
int his_index = 0;

struct termios orig_termios;

void printPromote();

void enableRawMode()
{
	tcgetattr(STDIN_FILENO, &orig_termios);
	struct termios raw = orig_termios;
	// 关回显，c_lflag是local flag的意思,ICANON是规范模式，即行缓冲模式，
	// 为什么要用位处理是因为c_lflag一般是一个32位或者64位的整数，而像ECHO和ICANON这种开关就是其中一位，
	// 也就是说对于ECHO和ICANON这种开关，就是一个所有位数上只有特定位为1的整数
	// 关回显是因为如果不关的话，输入的非正常字符如退格键，空格键等等都会显示出来，所以要关掉，然后自己来控制屏幕输出
	raw.c_lflag &= ~(ECHO | ICANON | ISIG);// ISIG是input signal的意思，关了之后终端对Ctrl+C这种特殊输入就是当成字符处理了，而不是中断程序
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);// 中间的参数指的是什么时候应用新设置
}

void disableRawMode()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
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


void printPromote()
{
	char address[PATH_MAX];

	cout << "sofei@" << getcwd(address, sizeof address);
	cout << ">>";
}

string read_line_with_history()
{
	cout << unitbuf;
	enableRawMode();

	string buffer;
	string saved_line;// 用户点上箭头时缓存当前行，因为当前行没被存进history

	while (true)
	{
		char ch;
		read(STDIN_FILENO, &ch, 1);

		if (ch == '\n' || ch == '\r')// r是回车
		{
			cout << endl;
			disableRawMode();
			history.push_back(buffer);
			his_index = history.size();
			return buffer;
		}
		else if (ch == 127)// 退格键
		{
			if (!buffer.empty())
			{
				buffer.pop_back();// 删除最后一个字符
				// 先左移光标然后用空格覆盖原来的字符，但是这时打印空格后光标又会右移，所以还需要左移一次
				std::cout << "\033[" << 1 << "D";
				cout << " ";
				std::cout << "\033[" << 1 << "D";
			}
		}
		else if (ch == 3)// Ctrl+C
		{
			cout << "^C\n";
			disableRawMode();// 不关的话终端会卡死，因为回显没开的话用户看不到输入的内容
			return " ";
		}
		else if (ch == '\033')// 033八进制，实际上是27，Esc键
		{
			// \033[A就是上箭头，A改成B就是下箭头
			read(STDIN_FILENO, &ch, 1);
			read(STDIN_FILENO, &ch, 1);
			if (ch == 'A' && his_index > 0 && !history.empty())
			{
				cout << "\033[2K\r";// \033[2K清除整行，但是不移动光标，\r将光标移到行首
				printPromote();
				if (his_index == history.size())
					saved_line = buffer;// 只存第一次按上箭头前的那一行
				buffer = history[his_index - 1];
				cout << buffer;
				his_index--;
			}
			else if (ch == 'B')
			{
				if (his_index + 1 >= history.size())
				{
					cout << "\033[2K\r";
					buffer = saved_line;
					printPromote();
					cout << buffer;
					his_index = history.size();
				}
				else
				{
					cout << "\033[2K\r";
					buffer = history[his_index + 1];
					printPromote();
					cout << buffer;
					his_index++;
				}
			}
		}
		else
		{
			buffer.push_back(ch);
			cout << ch;
		}
	}
}

void read_user_input()
{
	string input;
	printPromote();
	cout << flush;// 虽然在read_line_with_history中设置了unitbuf，但是这个函数在下面才调用
	while (true)
	{
		input = read_line_with_history();
		execute(eval(input));
	
		printPromote();
	}
}


int main()
{
	read_user_input();

}

