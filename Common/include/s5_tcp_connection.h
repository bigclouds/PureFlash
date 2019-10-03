#ifndef s5_tcp_connection_h__
#define s5_tcp_connection_h__
#include "s5_connection.h"
#include "s5_event_queue.h"
#include "s5_buffer.h"
#include "s5_poller.h"
#include "s5_utils.h"

class S5TcpConnection : public S5Connection
{
public:
	S5TcpConnection():sock_fd(0), poller(NULL), recv_buf(NULL), recved_len(0), wanted_recv_len(0),
		recv_bd(NULL),send_buf(NULL), sent_len(0), wanted_send_len(0),send_bd(NULL),
		readable(FALSE), writeable(FALSE),need_reconnect(FALSE){}
	virtual int post_recv(BufferDescriptor* buf);
	virtual int post_send(BufferDescriptor* buf);
	virtual int post_read(BufferDescriptor* buf);
	virtual int post_write(BufferDescriptor* buf);
	virtual int on_destroy();
	virtual int on_close();

	int init(int sock_fd, S5Poller *poller, int send_q_depth, int recv_q_depth);
	void destroy();

	int sock_fd;
	S5Poller *poller;

	void* recv_buf;
	int recved_len;              ///< how many has received.
	int wanted_recv_len;		///< want received length.
	BufferDescriptor* recv_bd;

	void* send_buf;
	int sent_len;				///< how many has sent
	int wanted_send_len;		///< want to send length of data.
	BufferDescriptor*  send_bd;

	BOOL readable;              ///< socket buffer have data to read yes or no.
	BOOL writeable;             ///< socket buffer can send data yes or no.

	BOOL need_reconnect;

	S5EventQueue recv_q;
	S5EventQueue send_q;

	std::string connection_info;
	struct s5_handshake_message handshake_msg;

	int do_receive();
	int do_send();

	static void on_send_q_event(int fd, uint32_t event, void* c);
	static void on_recv_q_event(int fd, uint32_t event, void* c);
	static void on_socket_event(int fd, uint32_t event, void* c);

private:
	void start_send(BufferDescriptor* bd);
	void start_recv(BufferDescriptor* bd);
	int rcv_with_error_handle();

};

#endif // s5_tcp_connection_h__