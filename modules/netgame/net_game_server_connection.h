#ifndef UDPSERVERCONNECTION_H
#define UDPSERVERCONNECTION_H

#include "reference.h"
#include "io/tcp_server.h"
#include "io/packet_peer_udp.h"
#include "modules/netgame/net_game_server.h"
#include "modules/netgame/net_game_server_data.h"

class NetGameServer;

class NetGameServerConnection: public Reference {
	OBJ_TYPE(NetGameServerConnection,Reference);

	Mutex *out_mutex;
	Vector<QueuedPacket*> tcp_queue;
	Ref<StreamPeerTCP> stream_peer;
	uint8_t server_time[256];
	uint8_t client_time[256];
	int udp_time;
	int tcp_time;
	int udp_ping;
	int tcp_ping;

	Error _get_tcp_packet(DVector<uint8_t> &pkt);
	bool _is_valid_time(uint8_t cmd, uint8_t time);
	void _send_udp_ping();
	void _send_tcp_ping();
	void _handle_tcp();
	void _handle_tcp_pcmd(DVector<uint8_t> &pkt, uint8_t pcmd);

public:
	Ref<PacketPeerStream> tcp;
	NetGameServer *server;
	CID id;
	CSE secret;
	IP_Address udp_host;
	ClientState state;
	int udp_port;
	bool authed;

	void on_update();
	void handle_udp(DVector<uint8_t> &pkt, IP_Address addr, int port);
	Error enqueue_tcp(const DVector<uint8_t> &pkt, uint8_t cmd);
	bool is_connected();
	DVector<uint8_t> build_pkt(QueuedPacket *qp);
	DVector<uint8_t> build_address_packet();

	NetGameServerConnection(CID id, CSE s, Ref<StreamPeerTCP> p,
				NetGameServer *srv);
	~NetGameServerConnection();
};


#endif
