#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define MAX_EVENTS 64

typedef int FD;
typedef void (*callback)(FD, char*, size_t);
typedef void (*readerror)(FD);

#define CHECK_BREAK(b) if (!(b)) break;

class Epoll {
private:
	FD epfd;
	FD listenfd;
	bool init;
	callback call;
	readerror readerr;
	struct epoll_event events[MAX_EVENTS];

public:
	Epoll(int port, callback func, readerror errfunc) {
		this->call = func;
		this->readerr = errfunc;
		for (int i = 0; i <= 0; ++i) {
			this->epfd = epoll_create(1);
			CHECK_BREAK(this->init = this->epfd != -1);
			this->listenfd = socket(AF_INET, SOCK_STREAM, 0);
			CHECK_BREAK(this->init = this->listenfd != 0);
			nonblocking(this->listenfd);
			CHECK_BREAK(this->init = this->bind(port));
			CHECK_BREAK(this->init = listen(this->listenfd, 5) == 0);
			struct epoll_event ev;
			ev.events = EPOLLIN | EPOLLET;
			ev.data.fd = this->listenfd;
			CHECK_BREAK(this->init = epoll_ctl(epfd, EPOLL_CTL_ADD, this->listenfd, &ev) != -1);
		}
	}

	~Epoll() {
		if (this->init) {
			close(this->epfd);
			close(this->listenfd);
		}
	}

	Epoll(const Epoll&) = delete;
	Epoll& operator=(const Epoll&) = delete;

	void nonblocking(FD fd) {
		int flags = fcntl(fd, F_GETFL, 0);
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	}

	bool bind(int port) {
		struct sockaddr_in servaddr;
		memset(&servaddr, 0, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
		servaddr.sin_port = htons(port);
		return ::bind(this->listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == 0;
	}

	void run() {
		if (!this->init)
			return;

		while (true) {
			int nfds = epoll_wait(this->epfd, events, MAX_EVENTS, -1);
			if (nfds == -1)
				break;

			for (int i = 0; i < nfds; i++) {
				int fd = events[i].data.fd;
				if (fd == this->listenfd)
					this->OnAccept();
				else if (events[i].events & EPOLLIN)
					this->OnRecv(fd);
				else if (events[i].events & EPOLLERR)
					this->OnClose(fd);
			}
		}
	}

	void OnAccept() {
		struct sockaddr_in cliaddr;
		socklen_t clilen = sizeof(cliaddr);
		while (true) {
			FD connfd = accept(this->listenfd, (struct sockaddr*)&cliaddr, &clilen);
			if (connfd == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					break;
				else {
					if (this->readerr)
						this->readerr(connfd);
					break;
				}
			}
			std::cout << "Accepted connection from " << inet_ntoa(cliaddr.sin_addr) << ":" << ntohs(cliaddr.sin_port) << std::endl;
			this->nonblocking(connfd);
			struct epoll_event ev;
			ev.events = EPOLLIN | EPOLLET;
			ev.data.fd = connfd;
			epoll_ctl(this->epfd, EPOLL_CTL_ADD, connfd, &ev);
		}
	}

	void OnRecv(FD fd) {
		char buf[1024] = { 0 };
		int n = 0;
		while (true) {
			n = read(fd, buf, sizeof(buf));
			if (n == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					break;
				else {
					if (this->readerr)
						this->readerr(fd);
					break;
				}
			}
			else if (n == 0)
				return this->OnClose(fd);
			else
				break;
		}

		if (this->call)
			this->call(fd, buf, n);
	}

	void OnClose(FD fd) {
		if (epoll_ctl(this->epfd, EPOLL_CTL_DEL, fd, NULL) == -1)
			return;

		close(fd);
		std::cout << "Closed connection from client " << fd << std::endl;
	}
};

void PrintMsg(FD fd, char* data, size_t size) {
	data[size] = '\0';
	printf("fd : %d, data : %s\n", fd, data);
	write(fd, "recv msg:", 9);
	write(fd, data, size);
}

void PrintReadError(FD fd) {
	printf("fd : %d read error\n", fd);
}

int main() {
	Epoll epoll(8888, PrintMsg, PrintReadError);
	epoll.run();
	return 0;
}
