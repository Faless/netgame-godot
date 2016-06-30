#ifndef UDP_SERVER_DATA_H
#define UDP_SERVER_DATA_H

#define SIGNAL_CLIENT_CONNECT "client_connect"
#define SIGNAL_CLIENT_READY "client_ready"
#define SIGNAL_CLIENT_DISCONNECT "client_disconnect"
#define SIGNAL_AUTH_PACKET "auth_packet"
#define SIGNAL_TCP_PACKET "tcp_packet"
#define SIGNAL_UDP_PACKET "udp_packet"

#define SERVER_SLEEP_USEC 50
#define CLIENT_SLEEP_USEC 200

#define PKT_QUEUE_SIZE 25
#define SIG_QUEUE_SIZE 25

#define TIMEOUT 15000
#define UDP_PING 500
#define TCP_PING 3000

#define CMD_MAX 255
#define PCMD_PING 0
#define PCMD_AUTH 1

typedef uint8_t CID;
typedef uint8_t CSE;

enum ClientState {
	WAIT_AUTH, WAIT_ACK, READY, DISCONNECTED
};

enum UDPSrvSig {
	CLI_CONNECTING, CLI_CONNECT, CLI_DISCONNECT, CLI_UDP, CLI_TCP
};

enum SignalsMode {
	PROCESS,
	FIXED,
	THREADED
};



struct QueuedPacket {
	CID id;
	uint8_t cmd;
	DVector<uint8_t> packet;
	bool timed;
};

struct QueuedSignal {
//	UDPSrvSig type;
	const char *signal;
	CID id;
	int cmd;
	DVector<uint8_t> packet;
	bool has_pkt;
};

VARIANT_ENUM_CAST(SignalsMode);

#endif
