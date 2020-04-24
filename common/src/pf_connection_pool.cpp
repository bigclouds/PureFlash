#include <mutex>
#include "pf_connection_pool.h"
#include "pf_connection.h"
#include "pf_tcp_connection.h"

using namespace  std;


int S5ConnectionPool::init(int size, S5Poller* poller, S5ClientVolumeInfo* vol, int io_depth, work_complete_handler _handler)
{
	pool_size = size;
	this->poller = poller;
	this->volume = vol;
	this->io_depth = io_depth;
	on_work_complete = _handler;
	return 0;
}

S5Connection* S5ConnectionPool::get_conn(const std::string& ip)
{
	std::lock_guard<std::mutex> _l(mtx);
	auto pos = ip_id_map.find(ip);
	if (pos != ip_id_map.end())
		return pos->second;
	S5TcpConnection *c = S5TcpConnection::connect_to_server(ip, 49162, poller, volume, io_depth, 4/*connection timeout*/);
	c->on_work_complete = on_work_complete;
	ip_id_map[ip] = c;
	c->add_ref();
	return c;
}



void S5ConnectionPool::close_all()
{
	S5LOG_INFO("Close all connection in pool, %d connections to release", ip_id_map.size());

	for(auto it = ip_id_map.begin(); it != ip_id_map.end(); ) {
		it->second->close();
		it->second->dec_ref();
	}
	ip_id_map.clear();
}