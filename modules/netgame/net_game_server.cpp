
#include <modules/netgame/net_game_server.h>

void NetGameServer::set_signal_mode(SignalsMode p_mode) {
	if(!quit)
	{
		_update_signal_mode();
	}
	signal_mode = p_mode;
}

void NetGameServer::_update_signal_mode()
{
	set_process(false);
	set_fixed_process(false);
	switch(signal_mode)
	{
		case PROCESS:
			set_process(true);
			break;
		case FIXED:
			set_fixed_process(true);
			break;
		case THREADED:
			// noop
			break;
	}
}

SignalsMode NetGameServer::get_signal_mode() const{
	return signal_mode;
}

void NetGameServer::_notification(int p_what) {
	if (
		(p_what==NOTIFICATION_PROCESS && signal_mode == PROCESS) ||
		(p_what==NOTIFICATION_FIXED_PROCESS && signal_mode == FIXED)) {
		// Flush signals
		while(signal_queue.size() > 0) {
			// Only this thread removes from the queue
			// it is safe to lock here
			signal_mutex->lock();
			QueuedSignal *qs = signal_queue.get(0);
			signal_queue.remove(0);
			signal_mutex->unlock();
			if(qs->has_pkt) {
				emit_signal(qs->signal, qs->id, qs->cmd, qs->packet);
			}
			else {
				emit_signal(qs->signal, qs->id);
			}
			memdelete(qs);
		}
	}
}

void NetGameServer::_server_tick() {
	if(quit) {
		return;
	}
	// Accept new connections
	_check_connections();

	// Handle incoming packets
	_handle_udp();

	// Update clients (handle tcp packets)
	_handle_tcp();

	// Cleanup disconnected clients
	_remove_stale_clients();

}

/**
 * Main UDP/TCP server loop
 */
void NetGameServer::_thread_start(void*s) {
	NetGameServer *self = (NetGameServer*) s;

	while (!self->quit) {
		self->_server_tick();
		OS::get_singleton()->delay_usec(SERVER_SLEEP_USEC);
	}

	// Close all connections
	self->_clear_clients();
}

void NetGameServer::_clear_queues() {
	// Clear UDP queue
	udp_mutex->lock();
	while(udp_queue.size() > 0) {
		QueuedPacket *qp = udp_queue.get(0);
		udp_queue.remove(0);
		memdelete(qp);
	}
	udp_mutex->unlock();

	// Clear Signal queue
	signal_mutex->lock();
	while(signal_queue.size() > 0) {
		QueuedSignal *qs = signal_queue.get(0);
		signal_queue.remove(0);
		memdelete(qs);
	}
	signal_mutex->unlock();
}

/***
 * Manage UDP packets
 */
void NetGameServer::_handle_tcp() {
	int i;

	conn_mutex->lock();
	for (i = 0; i < connections.size(); i++) {
		NetGameServerConnection *cd = connections.getv(i);
		cd->on_update();
	}
	conn_mutex->unlock();
}

/***
 * Manage UDP packets
 */
void NetGameServer::_handle_udp() {
	DVector<uint8_t> raw;
	NetGameServerConnection *cd;

	// Flush packets queue
	while(udp_queue.size() > 0) {
		// Only this thread removes from the queue
		// it is safe to lock here
		udp_mutex->lock();
		QueuedPacket *qp = udp_queue.get(0);
		udp_queue.remove(0);
		udp_mutex->unlock();

		NetGameServerConnection *cd = _get_client(qp->id);
		if(cd != NULL) {
			udp_server->set_send_address(cd->udp_host,
							cd->udp_port);
			udp_server->put_packet_buffer(cd->build_pkt(qp));
		}
		memdelete(qp);
	}

	// Handle incoming packets
	if(udp_server->get_available_packet_count() > 0) {
		udp_server->get_packet_buffer(raw);

		if(raw.size() < 1) {
			WARN_PRINT("Invalid UDP Packet!");
			return;
		}

		cd = _get_client(raw.get(0));
		if(cd == NULL) {
			WARN_PRINT("Invalid UDP Auth!");
			return;
		}

		cd->handle_udp(raw,
				udp_server->get_packet_address(),
				udp_server->get_packet_port());
	}
}

/***
 * Free client and send disconnect signal
 */
void NetGameServer::_delete_client(NetGameServerConnection *cd) {
	if(!quit) {
		_queue_signal(SIGNAL_CLIENT_DISCONNECT, cd->id);
	}
	memdelete(cd);
}

/**
 * Disconnect and remove dead clients
 */
void NetGameServer::_remove_stale_clients() {
	int i = 0;
	int id = 0;

	conn_mutex->lock();
	for (i = 0; i < connections.size(); ++i) {
		id = connections.get_array()[i].key;
		NetGameServerConnection *cd = connections.getv(i);
		if(!cd->is_connected()) {
			connections.erase(id);
			_delete_client(cd);
			i--;
		}
	}
	conn_mutex->unlock();
}

/***
 * Remove all clients
 */
void NetGameServer::_clear_clients() {
	int i = 0;

	conn_mutex->lock();
	for (i = 0; i < connections.size(); ++i) {
		_delete_client(connections.getv(i));
	}
	connections.empty();
	conn_mutex->unlock();
}

void NetGameServer::_check_connections() {
	// Accept new connections
	if (tcp_server->is_connection_available()) {

		conn_mutex->lock();
		NetGameServerConnection *cd = memnew(
			NetGameServerConnection(_get_id(), _get_secret(),
				tcp_server->take_connection(), this));
		connections.insert(cd->id, cd);
		conn_mutex->unlock();

		_queue_signal(SIGNAL_CLIENT_CONNECT, cd->id);
	}
}

void NetGameServer::_queue_signal(const char *sig, CID id)
{
	if(signal_mode == THREADED) {
		emit_signal(sig, id);
	}
	else {
		if(signal_queue.size() >= SIG_QUEUE_SIZE * (connections.size()+1)) {
			WARN_PRINT("SIGNAL QUEUE SIZE EXCEEDED");
			return;
		}
		QueuedSignal *qs = (QueuedSignal *) memnew(QueuedSignal);
		qs->id = id;
		qs->signal = sig;
		qs->cmd = -1;
		qs->has_pkt = false;
		signal_mutex->lock();
		signal_queue.insert(signal_queue.size(), qs);
		signal_mutex->unlock();
	}
}

void NetGameServer::_queue_signal(const char *sig, CID id,
				const DVector<uint8_t> &pkt, int cmd)
{
	if(signal_mode == THREADED) {
		emit_signal(sig, id, cmd, pkt);
	}
	else {
		QueuedSignal *qs = (QueuedSignal *) memnew(QueuedSignal);
		qs->id = id;
		qs->signal = sig;
		qs->cmd = cmd;
		qs->packet = pkt;
		qs->has_pkt = true;
		signal_mutex->lock();
		signal_queue.insert(signal_queue.size(), qs);
		signal_mutex->unlock();
	}
}

CID NetGameServer::_get_id() {
	int i = 0;
	CID last = 0;
	CID curr = 0;
	for (i = 0; i < connections.size(); ++i) {
		curr = connections.get_array()[i].key;
		if(curr > last+1) {
			return last+1;
		}
		last = curr;
	}
	if(last == 255) return 0;
	return last+1;
}

CSE NetGameServer::_get_secret() {
	return rand() % 256;
}

void NetGameServer::start(int tcp_port, int udp_port) {
	stop();
	_update_signal_mode();
	tcp_server->listen(tcp_port);
	udp_server->listen(udp_port);
	quit = false;
	thread = Thread::create(_thread_start, this);
}

void NetGameServer::stop() {
	if (thread != NULL) {
		quit = true;
		Thread::wait_to_finish(thread);
		memdelete(thread);
		tcp_server->stop();
		udp_server->close();
		_clear_queues();
	}

	thread = NULL;
}

NetGameServerConnection *NetGameServer::_get_client(int id) {
	int index = connections.find(id);
	if(index == -1) {
		return NULL;
	}
	return connections.getv(index);
}

Error NetGameServer::put_tcp_packet(int id, const DVector<uint8_t> &pkt, int cmd) {
	Error out;

	conn_mutex->lock();
	NetGameServerConnection *cd = _get_client(id);
	if(cd == NULL) {
		conn_mutex->unlock();
		return ERR_DOES_NOT_EXIST;
	}
	out = cd->enqueue_tcp(pkt, cmd);
	conn_mutex->unlock();
	return out;
}

Error NetGameServer::put_udp_packet(int id, const DVector<uint8_t> &pkt,
					int cmd, bool timed) {
	conn_mutex->lock();
	NetGameServerConnection *cd = _get_client(id);
	if(cd == NULL) {
		conn_mutex->unlock();
		return ERR_DOES_NOT_EXIST;
	}
	if(cd->state != READY) {
		conn_mutex->unlock();
		return ERR_CONNECTION_ERROR;
	}
	conn_mutex->unlock();
	return _enqueue_udp(id, pkt, cmd, timed);
}

Error NetGameServer::broadcast_udp(const DVector<uint8_t> &pkt, int cmd,
					bool timed) {
	int i;
	conn_mutex->lock();
	for(i=0; i<connections.size(); i++) {
		NetGameServerConnection *cd = connections.getv(i);
		_enqueue_udp(cd->id, pkt, cmd, timed);
	}
	conn_mutex->unlock();
	return OK;
}

Error NetGameServer::broadcast_tcp(const DVector<uint8_t> &pkt, int cmd) {
	int i;
	conn_mutex->lock();
	for(i=0; i<connections.size(); i++) {
		NetGameServerConnection *cd = connections.getv(i);
		cd->enqueue_tcp(pkt, cmd);
	}
	conn_mutex->unlock();
	return OK;
}

/*
 * Enqueue packet (the thread will send it)
 */
Error NetGameServer::_enqueue_udp(CID id, const DVector<uint8_t> &pkt,
				int cmd, bool timed) {
	if(udp_queue.size() >= PKT_QUEUE_SIZE * (connections.size()+1)) {
		WARN_PRINT("UDP QUEUE SIZE EXCEEDED");
		return ERR_OUT_OF_MEMORY;
	}

	QueuedPacket *qp = (QueuedPacket *) memnew(QueuedPacket);
	qp->id = id;
	qp->cmd = cmd;
	qp->packet = pkt;
	qp->timed = timed;
	udp_mutex->lock();
	udp_queue.insert(udp_queue.size(), qp);
	udp_mutex->unlock();
	return OK;
}

Error NetGameServer::auth_client(CID id) {
	conn_mutex->lock();
	NetGameServerConnection *conn = _get_client(id);
	if(conn == NULL) {
		conn_mutex->unlock();
		return ERR_DOES_NOT_EXIST;
	}
	if(conn->authed) {
		conn_mutex->unlock();
		return ERR_ALREADY_EXISTS;
	}
	conn->authed = true;

	// Assign id to client
	unsigned char raw[4];
	raw[0] = CMD_MAX;
	raw[1] = PCMD_AUTH;
	raw[2] = conn->id;
	raw[3] = conn->secret;
	conn->tcp->put_packet(raw, 4);

	conn_mutex->unlock();
	return OK;
}

Error NetGameServer::kick_client(CID id) {
	conn_mutex->lock();
	NetGameServerConnection *conn = _get_client(id);
	if(conn == NULL) {
		conn_mutex->unlock();
		return ERR_DOES_NOT_EXIST;
	}

	this->connections.erase(conn->id);
	conn_mutex->unlock();

	_delete_client(conn);
	return OK;
}

void NetGameServer::_bind_methods() {
	ADD_SIGNAL(MethodInfo(SIGNAL_CLIENT_CONNECT,PropertyInfo( Variant::INT,"id")));
	ADD_SIGNAL(MethodInfo(SIGNAL_CLIENT_READY,PropertyInfo( Variant::INT,"id")));
	ADD_SIGNAL(MethodInfo(SIGNAL_CLIENT_DISCONNECT,PropertyInfo( Variant::INT,"id")));

	ADD_SIGNAL(MethodInfo(SIGNAL_UDP_PACKET,PropertyInfo(Variant::INT,"id"), PropertyInfo(Variant::INT,"cmd"), PropertyInfo(Variant::RAW_ARRAY,"pkt")));
	ADD_SIGNAL(MethodInfo(SIGNAL_TCP_PACKET,PropertyInfo(Variant::INT,"id"), PropertyInfo(Variant::INT,"cmd"), PropertyInfo(Variant::RAW_ARRAY,"pkt")));
	ADD_SIGNAL(MethodInfo(SIGNAL_AUTH_PACKET,PropertyInfo(Variant::INT,"id"), PropertyInfo(Variant::INT,"cmd"), PropertyInfo(Variant::RAW_ARRAY,"pkt")));

	BIND_CONSTANT(PROCESS);
	BIND_CONSTANT(FIXED);
	BIND_CONSTANT(THREADED);

	ObjectTypeDB::bind_method(_MD("start", "tcp_port", "udp_port"), &NetGameServer::start);
	ObjectTypeDB::bind_method("stop", &NetGameServer::stop);
	ObjectTypeDB::bind_method(_MD("put_udp_packet:Error", "id", "pkt", "cmd", "rt"),&NetGameServer::put_udp_packet,DEFVAL(0), DEFVAL(false));
	ObjectTypeDB::bind_method(_MD("put_tcp_packet:Error", "id", "pkt", "cmd"),&NetGameServer::put_tcp_packet, DEFVAL(0));
	ObjectTypeDB::bind_method(_MD("broadcast_udp:Error", "pkt", "cmd", "rt"),&NetGameServer::broadcast_udp,DEFVAL(0), DEFVAL(false));
	ObjectTypeDB::bind_method(_MD("broadcast_tcp:Error", "pkt", "cmd"),&NetGameServer::broadcast_tcp, DEFVAL(0));
	ObjectTypeDB::bind_method(_MD("auth_client", "id"),&NetGameServer::auth_client);
	ObjectTypeDB::bind_method(_MD("kick_client", "id"),&NetGameServer::kick_client);
	ObjectTypeDB::bind_method(_MD("set_signal_mode","mode"),&NetGameServer::set_signal_mode);
	ObjectTypeDB::bind_method(_MD("get_signal_mode"),&NetGameServer::get_signal_mode);
	ADD_PROPERTYNZ( PropertyInfo(Variant::INT,"signal_mode",PROPERTY_HINT_ENUM,"Process,Fixed,Threaded"),_SCS("set_signal_mode"),_SCS("get_signal_mode"));
}

NetGameServer::NetGameServer() {
	signal_mode = PROCESS;
	quit = true;
	udp_mutex = Mutex::create();
	conn_mutex = Mutex::create();
	signal_mutex = Mutex::create();
	tcp_server = TCP_Server::create_ref();
	udp_server = PacketPeerUDP::create_ref();
	thread = NULL;
}

NetGameServer::~NetGameServer() {
	if (thread != NULL) {
		quit = true;
		Thread::wait_to_finish(thread);
		memdelete(thread);
	}

	memdelete(udp_mutex);
	memdelete(conn_mutex);
	memdelete(signal_mutex);
}
