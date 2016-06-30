#ifndef UDPSERVER_H
#define UDPSERVER_H

#include <stdlib.h>
#include "reference.h"
#include "os/thread.h"
#include "io/tcp_server.h"
#include "io/packet_peer_udp.h"
#include "scene/main/node.h"
#include "modules/netgame/net_game_server_data.h"
#include "modules/netgame/net_game_server_connection.h"

class NetGameServerConnection;

class NetGameServer: public Node {
	OBJ_TYPE( NetGameServer, Node );

	Mutex *conn_mutex;
	Mutex *udp_mutex;
	Mutex *signal_mutex;
	Ref<TCP_Server> tcp_server;
	Vector<QueuedPacket*> udp_queue;
	Vector<QueuedSignal*> signal_queue;
	VMap<CID, NetGameServerConnection*> connections;
	Thread *thread;
	bool quit;

	CID _get_id();
	CSE _get_secret();

	void _server_tick();
	Error _enqueue_udp(CID id, const DVector<uint8_t> &pkt, int cmd, bool timed);
	void _check_connections();
	void _delete_client(NetGameServerConnection *cd);
	void _clear_clients();
	void _remove_stale_clients();
	void _handle_udp();
	void _handle_tcp();
	void _update_signal_mode();

	NetGameServerConnection* _handle_udp_handshake(CID id, CSE secret);
	NetGameServerConnection *_get_client(int id);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	Ref<PacketPeerUDP> udp_server;
	SignalsMode signal_mode;

	void start(int tcp_port, int udp_port);
	void stop();
	void set_signal_mode(SignalsMode p_mode);
	SignalsMode get_signal_mode() const;

	void _queue_signal(const char *sig, CID id);
	void _queue_signal(const char *sig, CID id,
				const DVector<uint8_t> &pkt, int cmd=0);

	Error put_tcp_packet(int id, const DVector<uint8_t> &pkt, int cmd=0);
	Error broadcast_tcp(const DVector<uint8_t> &pkt, int cmd=0);
	Error put_udp_packet(int id, const DVector<uint8_t> &pkt,
				int cmd=0, bool timed=false);
	Error broadcast_udp(const DVector<uint8_t> &pkt,
				int cmd=0, bool timed=false);
	Error auth_client(CID id);
	Error kick_client(CID id);

	static void _thread_start(void*s);


	NetGameServer();
	~NetGameServer();
};

#endif
