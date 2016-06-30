
#include <modules/netgame/net_game_client.h>

void NetGameClient::set_signal_mode(SignalsMode p_mode) {
	if(!quit)
	{
		_update_signal_mode();
	}
	signal_mode = p_mode;
}

void NetGameClient::_update_signal_mode()
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

SignalsMode NetGameClient::get_signal_mode() const{
	return signal_mode;
}

void NetGameClient::_notification(int p_what) {
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
			if(qs->has_pkt)
				emit_signal(qs->signal, qs->id,
						qs->cmd, qs->packet);
			else
				emit_signal(qs->signal, qs->id);
			memdelete(qs);
		}
	}
}

/**
 * Main UDP/TCP server loop
 */
void NetGameClient::_thread_start(void*s) {

	int time = OS::get_singleton()->get_ticks_msec();
	int t_udp = time;
	int t_tcp = time;
	int t_udp_ping = time;
	int t_tcp_ping = time;
	NetGameClient *self = (NetGameClient*) s;
	StreamPeerTCP::Status status;

	while (!self->quit && self->state != DISCONNECTED) {
		// Check streams state
		time = OS::get_singleton()->get_ticks_msec();
		if(self->tcp->get_available_packet_count()) {
			t_tcp = time;
			self->_handle_tcp();
		}
		if(self->udp->get_available_packet_count()) {
			t_udp = time;
			self->_handle_udp();
		}
		// Skip udp timeout while in pre-auth mode
		if(self->state == WAIT_AUTH) {
			t_udp = time;
		}
		// Check timeout
		if(t_udp + TIMEOUT < time || t_tcp + TIMEOUT < time) {
			self->state = DISCONNECTED;
			break;
		}
		// Send UDP ping
		if(t_udp_ping + UDP_PING < time) {
			self->_send_udp_ping();
			t_udp_ping = time;
		}
		// Send TCP ping
		if(t_tcp_ping + TCP_PING < time) {
			self->_send_tcp_ping();
			t_tcp_ping = time;
		}

		self->_flush_packets();

		// Break if we got disconnected
		status = self->tcp_stream->get_status();
		if(status == StreamPeerTCP::STATUS_NONE ||
			status == StreamPeerTCP::STATUS_ERROR) {
			self->state = DISCONNECTED;
			break;
		}

		// Sleep a bit
		OS::get_singleton()->delay_usec(CLIENT_SLEEP_USEC);
	}

	// Notify disconnection
	self->_queue_signal(SIGNAL_CLIENT_DISCONNECT, self->client_id);
}

void NetGameClient::_clear_queues() {
	// Clear TCP queue
	tcp_mutex->lock();
	while(tcp_queue.size() > 0) {
		QueuedPacket *qp = tcp_queue.get(0);
		tcp_queue.remove(0);
		memdelete(qp);
	}
	tcp_mutex->unlock();

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

void NetGameClient::_flush_packets() {
	// Flush tcp
	while(tcp_queue.size() > 0) {
		// Only this thread removes from the queue
		// it is safe to lock here
		tcp_mutex->lock();
		QueuedPacket *qp = tcp_queue.get(0);
		tcp_queue.remove(0);
		tcp_mutex->unlock();
		tcp->put_packet_buffer(qp->packet);
		memdelete(qp);
	}

	// Flush udp
	while(udp_queue.size() > 0) {
		// Only this thread removes from the queue
		// it is safe to lock here
		udp_mutex->lock();
		QueuedPacket *qp = udp_queue.get(0);
		udp_queue.remove(0);
		udp_mutex->unlock();
		udp->put_packet_buffer(qp->packet);
		memdelete(qp);
	}
}

/***
 * Manage TCP packets
 */
void NetGameClient::_handle_tcp() {
	uint8_t cmd, scmd;
	DVector<uint8_t> pkt;
	tcp->get_packet_buffer(pkt);

	if(pkt.size() < 2)
		return;

	cmd = pkt.get(0);
	scmd = pkt.get(1);
	pkt.remove(0);
	pkt.remove(0);

	if(cmd == CMD_MAX) {
		_handle_tcp_pcmd(pkt, scmd);
	}
	else if(state == WAIT_AUTH) {
		_queue_signal(SIGNAL_AUTH_PACKET, client_id, pkt, cmd);
	}
	else if(state == READY) {
		_queue_signal(SIGNAL_TCP_PACKET, client_id, pkt, cmd);
	}
}


void NetGameClient::_handle_tcp_pcmd(DVector<uint8_t> pkt, uint8_t pcmd) {
	if(pcmd == PCMD_AUTH) {
		if(pkt.size() != 2) {
			// Invalid auth packet
			return;
		}

		// Auth received
		client_id = pkt.get(0);
		client_secret = pkt.get(1);
		has_id = true;
		state = WAIT_ACK;
		_queue_signal(SIGNAL_CLIENT_CONNECT, client_id);
	}
}

/***
 * Manage UDP packets
 */
void NetGameClient::_handle_udp() {
	uint8_t cmd, time;
	DVector<uint8_t> pkt;

	udp->get_packet_buffer(pkt);

	// Invalid packet
	if(pkt.size() < 2) {
		return;
	}
	cmd = pkt.get(0);
	time = pkt.get(1);
	pkt.remove(0);
	pkt.remove(0);

	// Protocol command
	if(cmd == CMD_MAX) {
		_handle_udp_pcmd(pkt, time);
		return;
	}

	// Check size and time
	if(pkt.size() < 1 || !_is_valid_time(cmd, time)) {
		return;
	}

	if(time != 0) {
		server_time[cmd] = time;
	}

	// Queue signal
	_queue_signal(SIGNAL_UDP_PACKET, client_id, pkt, cmd);
}

void NetGameClient::_handle_udp_pcmd(DVector<uint8_t> pkt, uint8_t pcmd) {
	if(pcmd == PCMD_AUTH && state != READY) {
		if(pkt.size() != 6) {
			// Auth failed, disconnecting
			state = DISCONNECTED;
			return;
		}

		// Send reply
		DVector<uint8_t> out;
		out.append(CMD_MAX);
		out.append(PCMD_AUTH);
		out.append_array(pkt);
		tcp->put_packet_buffer(out);

		// Authed
		_queue_signal(SIGNAL_CLIENT_READY, client_id);
		state = READY;
	}
}

bool NetGameClient::_is_valid_time(uint8_t cmd, uint8_t time) {
	return time == 0 || server_time[cmd] == 0 ||
		(server_time[cmd] < time && (time - server_time[cmd]) < 128) ||
		(time < server_time[cmd] && (server_time[cmd] - time) > 128);
}

void NetGameClient::_send_tcp_ping() {
	uint8_t raw[2];

	raw[0] = CMD_MAX;
	raw[1] = PCMD_PING;
	tcp->put_packet(raw, 2);
}

void NetGameClient::_send_udp_ping() {
	if(!has_id) return;

	uint8_t raw[4];

	raw[0] = client_id;
	raw[1] = client_secret;
	raw[2] = CMD_MAX;
	raw[3] = PCMD_PING;
	udp->put_packet(raw, 4);
}

void NetGameClient::_queue_signal(const char *sig, CID id)
{
	if(signal_mode == THREADED) {
		emit_signal(sig, id);
	}
	else {
		if(signal_queue.size() >= SIG_QUEUE_SIZE) {
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

void NetGameClient::_queue_signal(const char *sig, CID id,
				const DVector<uint8_t> pkt, int cmd)
{
	if(signal_mode == THREADED) {
		emit_signal(sig, id, cmd, pkt);
	}
	else {
		QueuedSignal *qs = (QueuedSignal *) memnew(QueuedSignal);
		qs->id = id;
		qs->signal = sig;
		qs->packet = pkt;
		qs->cmd = cmd;
		qs->has_pkt = true;
		signal_mutex->lock();
		signal_queue.insert(signal_queue.size(), qs);
		signal_mutex->unlock();
	}
}
void NetGameClient::connect_to(const String &host, int tcp_port, int udp_port) {
	int i;
	for(i = 0; i < 256; i++) {
		server_time[i] = 0;
		client_time[i] = 1;
	}

	IP_Address addr = IP_Address(host);

	close();
	_update_signal_mode();
	tcp_stream->connect(addr, tcp_port);
	udp->set_send_address(addr, udp_port);
	quit = false;
	thread = Thread::create(_thread_start, this);
}

void NetGameClient::close() {

	if (thread != NULL) {
		quit = true;
		Thread::wait_to_finish(thread);
		memdelete(thread);
		tcp_stream->disconnect();
		udp->close();
		_clear_queues();
	}
	thread = NULL;
}

DVector<uint8_t> NetGameClient::_build_tcp(DVector<uint8_t> pkt, uint8_t cmd) {
	DVector<uint8_t> out;

	out.append(cmd);
	out.append(0);
	out.append_array(pkt);

	return out;
}

DVector<uint8_t> NetGameClient::_build_udp(DVector<uint8_t> pkt,
						uint8_t cmd, bool timed) {
	DVector<uint8_t> out;

	out.append(client_id);
	out.append(client_secret);
	out.append(cmd);

	if(timed) {
		out.append(client_time[cmd]);
		client_time[cmd] += 1;
		if(client_time[cmd] == 0) {
			client_time[cmd] = 1;
		}
	}
	else {
		out.append(0);
	}
	out.append_array(pkt);

	return out;
}

Error NetGameClient::put_tcp_packet(const DVector<uint8_t> &pkt, int cmd) {
	if(state == DISCONNECTED) {
		return ERR_CONNECTION_ERROR;
	}
	if(tcp_queue.size() >= PKT_QUEUE_SIZE) {
		WARN_PRINT("TCP QUEUE SIZE EXCEEDED");
		return ERR_OUT_OF_MEMORY;
	}

	DVector<uint8_t> out;

	QueuedPacket *qp = (QueuedPacket *) memnew(QueuedPacket);
	qp->packet = _build_tcp(pkt, cmd);
	tcp_mutex->lock();
	tcp_queue.insert(tcp_queue.size(), qp);
	tcp_mutex->unlock();

	return OK;
}

Error NetGameClient::put_udp_packet(const DVector<uint8_t> &pkt,
					int cmd, bool timed) {
	if(state != READY) {
		return ERR_CONNECTION_ERROR;
	}
	if(udp_queue.size() >= PKT_QUEUE_SIZE) {
		WARN_PRINT("UDP QUEUE SIZE EXCEEDED");
		return ERR_OUT_OF_MEMORY;
	}

	QueuedPacket *qp = (QueuedPacket *) memnew(QueuedPacket);
	qp->packet = _build_udp(pkt, cmd, timed);
	udp_mutex->lock();
	udp_queue.insert(udp_queue.size(), qp);
	udp_mutex->unlock();

	return OK;
}

void NetGameClient::_bind_methods() {
	ADD_SIGNAL(MethodInfo(SIGNAL_CLIENT_CONNECT,PropertyInfo( Variant::INT,"id")));
	ADD_SIGNAL(MethodInfo(SIGNAL_CLIENT_READY,PropertyInfo( Variant::INT,"id")));
	ADD_SIGNAL(MethodInfo(SIGNAL_CLIENT_DISCONNECT,PropertyInfo( Variant::INT,"id")));

	ADD_SIGNAL(MethodInfo(SIGNAL_UDP_PACKET,PropertyInfo( Variant::INT,"id"), PropertyInfo( Variant::INT,"cmd"), PropertyInfo( Variant::RAW_ARRAY,"pkt")));
	ADD_SIGNAL(MethodInfo(SIGNAL_TCP_PACKET,PropertyInfo( Variant::INT,"id"), PropertyInfo( Variant::INT,"cmd"), PropertyInfo( Variant::RAW_ARRAY,"pkt")));
	ADD_SIGNAL(MethodInfo(SIGNAL_AUTH_PACKET,PropertyInfo( Variant::INT,"id"), PropertyInfo( Variant::INT,"cmd"), PropertyInfo( Variant::RAW_ARRAY,"pkt")));

	BIND_CONSTANT(PROCESS);
	BIND_CONSTANT(FIXED);
	BIND_CONSTANT(THREADED);

	ObjectTypeDB::bind_method("connect_to", &NetGameClient::connect_to);
	ObjectTypeDB::bind_method("close", &NetGameClient::close);
	ObjectTypeDB::bind_method(_MD("put_udp_packet:Error", "pkt", "cmd", "rt"),&NetGameClient::put_udp_packet,DEFVAL(0),DEFVAL(false));
	ObjectTypeDB::bind_method(_MD("put_tcp_packet:Error", "pkt", "cmd"),&NetGameClient::put_tcp_packet,DEFVAL(0));
	ObjectTypeDB::bind_method(_MD("set_signal_mode","mode"),&NetGameClient::set_signal_mode);
	ObjectTypeDB::bind_method(_MD("get_signal_mode"),&NetGameClient::get_signal_mode);
	ADD_PROPERTYNZ( PropertyInfo(Variant::INT,"signal_mode",PROPERTY_HINT_ENUM,"Process,Fixed,Threaded"),_SCS("set_signal_mode"),_SCS("get_signal_mode"));}

NetGameClient::NetGameClient() {
	signal_mode = PROCESS;
	state = WAIT_AUTH;
	client_id = 0;
	client_secret = 0;
	quit = true;
	has_id = false;
	udp_mutex = Mutex::create();
	tcp_mutex = Mutex::create();
	signal_mutex = Mutex::create();
	tcp_stream = StreamPeerTCP::create_ref();
	tcp = Ref<PacketPeerStream>( memnew(PacketPeerStream) );
	tcp->set_stream_peer(tcp_stream);
	udp = PacketPeerUDP::create_ref();
	thread = NULL;
}


NetGameClient::~NetGameClient() {
	close();
	memdelete(signal_mutex);
	memdelete(udp_mutex);
	memdelete(tcp_mutex);
}
