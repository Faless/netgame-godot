#ifndef UDPCLIENT_H
#define UDPCLIENT_H

#include "reference.h"
#include "os/thread.h"
#include "io/tcp_server.h"
#include "scene/main/node.h"
#include "io/packet_peer_udp.h"
#include "modules/netgame/net_game_server_data.h"

class NetGameClient: public Node {
	OBJ_TYPE(NetGameClient,Node);

	typedef uint8_t ClientID;
	typedef uint8_t ClientSecret;

	enum ClientState {
		WAIT_AUTH, WAIT_ACK, READY, DISCONNECTED
	};

	ClientID client_id;
	ClientSecret client_secret;
	ClientState state;

	Mutex *udp_mutex;
	Mutex *tcp_mutex;
	Mutex *signal_mutex;
	Ref<StreamPeerTCP> tcp_stream;
	Ref<PacketPeerStream> tcp;
	Ref<PacketPeerUDP> udp;
	Vector<QueuedPacket*> udp_queue;
	Vector<QueuedPacket*> tcp_queue;
	Vector<QueuedSignal*> signal_queue;
	Thread *thread;
	bool quit;
	bool has_id;
	uint8_t client_time[256];
	uint8_t server_time[256];


	void _check_connection();
	void _send_udp_ping();
	void _send_tcp_ping();
	void _handle_udp();
	void _handle_tcp();
	void _handle_udp_pcmd(DVector<uint8_t> pkt, uint8_t pcmd);
	void _handle_tcp_pcmd(DVector<uint8_t> pkt, uint8_t pcmd);
	void _update_signal_mode();
	void _flush_packets();
	void _clear_queues();

	void _queue_signal(const char *sig, CID id);
	void _queue_signal(const char *sig, CID id,
				const DVector<uint8_t> pkt, int cmd);

	DVector<uint8_t> _build_tcp(DVector<uint8_t> pkt, uint8_t cmd);
	DVector<uint8_t> _build_udp(DVector<uint8_t> pkt,
					uint8_t cmd, bool timed);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	SignalsMode signal_mode;

	void connect_to(const String &host, int tcp_port, int udp_port);
	bool _is_valid_time(uint8_t cmd, uint8_t time);
	void close();
	Error put_tcp_packet(const DVector<uint8_t> &pkt, int cmd=0);
	Error put_udp_packet(const DVector<uint8_t> &pkt,
				int cmd=0, bool timed=false);
	void set_signal_mode(SignalsMode p_mode);
	SignalsMode get_signal_mode() const;

	static void _thread_start(void*s);
	NetGameClient();
	~NetGameClient();
};

#endif
