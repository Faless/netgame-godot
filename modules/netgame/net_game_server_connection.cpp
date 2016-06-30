
#include "modules/netgame/net_game_server_connection.h"

void NetGameServerConnection::on_update() {
	int time = OS::get_singleton()->get_ticks_msec();

	if (!is_connected()) {
		state = DISCONNECTED;
		if(stream_peer->is_connected()) {
			stream_peer->disconnect();
		}
		return;
	}

	time = OS::get_singleton()->get_ticks_msec();

	if(tcp->get_available_packet_count() > 0) {
		_handle_tcp();
		tcp_time = time;
	}

	if(udp_ping + UDP_PING < time) {
		_send_udp_ping();
		udp_ping = time;
	}

	if(tcp_ping + TCP_PING < time) {
		_send_tcp_ping();
		tcp_ping = time;
	}

	// Flush tcp queue
	while(tcp_queue.size() > 0) {
		// This thread is the only one removing from the queue
		// locking only for remove is safe
		QueuedPacket *qp = tcp_queue.get(0);
		out_mutex->lock();
		tcp_queue.remove(0);
		out_mutex->unlock();

		tcp->put_packet_buffer(build_pkt(qp));
		memdelete(qp);

	}
}

void NetGameServerConnection::_send_tcp_ping() {
	uint8_t raw[2];

	raw[0] = CMD_MAX;
	raw[1] = PCMD_PING;

	// TCP Ping
	tcp->put_packet(raw, 2);
}

void NetGameServerConnection::_send_udp_ping() {
	if(state != READY) return;

	uint8_t raw[2];

	raw[0] = CMD_MAX;
	raw[1] = PCMD_PING;
	// UDP Ping
	server->udp_server->set_send_address(udp_host, udp_port);
	server->udp_server->put_packet(raw, 2);
}

void NetGameServerConnection::_handle_tcp() {
	uint8_t cmd, pcmd;
	DVector<uint8_t> pkt;

	tcp->get_packet_buffer(pkt);

	if(pkt.size() < 2) {
		// Invalid packet
		return;
	}

	cmd = pkt.get(0);
	pcmd = pkt.get(1);
	pkt.remove(0);
	pkt.remove(0);

	if(cmd == CMD_MAX) {
		_handle_tcp_pcmd(pkt, pcmd);
		return;
	}

	// Size check
	if(pkt.size() < 1) {
		return;
	}

	if(!authed) {
		server->_queue_signal(SIGNAL_AUTH_PACKET, id, pkt, cmd);
	}
	else {
		server->_queue_signal(SIGNAL_TCP_PACKET, id, pkt, cmd);
	}
}

void NetGameServerConnection::_handle_tcp_pcmd(DVector<uint8_t> &pkt,
						uint8_t pcmd) {
	if(pcmd == PCMD_AUTH) {
		if(!authed || state != WAIT_AUTH || pkt.size() < 6) {
			return;
		}
		IP_Address host;
		int port = 0;
		host.field[0] = pkt[0];
		host.field[1] = pkt[1];
		host.field[2] = pkt[2];
		host.field[3] = pkt[3];
		port |= ((pkt[4]<<8) | pkt[5]);

		// Invalid auth
		if(host != udp_host || port != udp_port) {
			state = DISCONNECTED;
			return;
		}

		state = READY;
		server->_queue_signal(SIGNAL_CLIENT_READY, id);
	}
}

void NetGameServerConnection::handle_udp(DVector<uint8_t> &pkt,
						IP_Address addr, int port) {

	uint8_t id, secret, cmd, time;

	if(pkt.size() < 4) {
		WARN_PRINT("Invalid UDP Packet!");
		return;
	}

	id = pkt.get(0);
	secret = pkt.get(1);
	cmd = pkt.get(2);
	time = pkt.get(3);

	pkt.remove(0);
	pkt.remove(0);
	pkt.remove(0);
	pkt.remove(0);

	// Invalid secret
	if(secret != this->secret)
		return;

	// If the client is waiting first packets then set addr and port
	// and reply back with his addr and port
	if(state == WAIT_AUTH) {
		udp_time = OS::get_singleton()->get_ticks_msec();
		udp_port = port;
		udp_host = addr;
		server->udp_server->set_send_address(addr, port);
		server->udp_server->put_packet_buffer(build_address_packet());
		return;
	}
	// If the client was already authed we need to verify that its
	// IP and port are correct
	else if(udp_port != port || udp_host != addr) {
		WARN_PRINT("Illegal UDP packet received");
		return;
	}

	udp_time = OS::get_singleton()->get_ticks_msec();
	if(cmd == CMD_MAX) {
		// TODO handle udp protocol commands
		return;
	}

	// Check auth, size, time
	if(!authed || pkt.size() < 1 || !_is_valid_time(cmd, time)) {
		return;
	}

	if(time != 0) {
		client_time[cmd] = time;
	}

	server->_queue_signal(SIGNAL_UDP_PACKET, id, pkt, cmd);
}

DVector<uint8_t> NetGameServerConnection::build_pkt(QueuedPacket *qp) {
	DVector<uint8_t> out;
	uint8_t cmd = qp->cmd;
	out.append(cmd);

	if(qp->timed) {
		out.append(server_time[cmd]);
		server_time[cmd] += 1;
		if(server_time[cmd] == 0) {
			server_time[cmd] = 1;
		}
	}
	else {
		out.append(0);
	}
	out.append_array(qp->packet);
	return out;
}

bool NetGameServerConnection::_is_valid_time(uint8_t cmd, uint8_t time) {
	return time == 0 || client_time[cmd] == 0 ||
		(client_time[cmd] < time && (time - client_time[cmd]) < 128) ||
		(time < client_time[cmd] && (client_time[cmd] - time) > 128);
}

bool NetGameServerConnection::is_connected() {
	int time = OS::get_singleton()->get_ticks_msec();
	return state != DISCONNECTED
		&& stream_peer->is_connected()
		&& udp_time + TIMEOUT > time
		&& tcp_time + TIMEOUT > time;
}

DVector<uint8_t> NetGameServerConnection::build_address_packet() {
	DVector<uint8_t> pkt;
	pkt.append(CMD_MAX);
	pkt.append(PCMD_AUTH);
	pkt.append(udp_host.field[0]);
	pkt.append(udp_host.field[1]);
	pkt.append(udp_host.field[2]);
	pkt.append(udp_host.field[3]);
	pkt.append(udp_port>>8);
	pkt.append(udp_port);
	return pkt;
}

Error NetGameServerConnection::enqueue_tcp(const DVector<uint8_t> &pkt, uint8_t cmd) {
	if(tcp_queue.size() >= PKT_QUEUE_SIZE) {
		WARN_PRINT("TCP QUEUE SIZE EXCEEDED");
		return ERR_OUT_OF_MEMORY;
	}

	QueuedPacket *qp = (QueuedPacket *) memnew(QueuedPacket);
	qp->id = id;
	qp->packet = pkt;
	qp->cmd = cmd;
	qp->timed = false;
	out_mutex->lock();
	tcp_queue.insert(tcp_queue.size(), qp);
	out_mutex->unlock();
	return OK;
}

NetGameServerConnection::NetGameServerConnection(CID id, CSE s, Ref<StreamPeerTCP> p, NetGameServer *srv) {
	int i;

	for(i = 0; i < 256; i++)
	{
		client_time[i] = 0;
		server_time[i] = 1;
	}

	this->id = id;
	secret = s;
	stream_peer = p;
	tcp = Ref<PacketPeerStream>( memnew(PacketPeerStream) );
	tcp->set_stream_peer(stream_peer);
	state = WAIT_AUTH;
	udp_ping = OS::get_singleton()->get_ticks_msec();
	udp_time = udp_ping;
	tcp_time = udp_ping;
	tcp_ping = udp_ping;
	udp_port = 0;
	authed = false;
	server = srv;
	out_mutex = Mutex::create();
}

NetGameServerConnection::~NetGameServerConnection() {
	memdelete(out_mutex);
}
