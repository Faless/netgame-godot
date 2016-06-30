
extends Panel

var client = preload("res://client.xscn")

var clients = []
var container
var server_log
var server
var scroll

var tcp_port = 4666
var udp_port = 4666

func _ready():
	scroll = get_node("ScrollContainer")
	server_log = get_node("ScrollContainer/ServerLog")
	container = get_node("Container")
	server = get_node("ScrollContainer/NetGameServer")
	
	# Sent when a client disconnects
	server.connect("client_disconnect", self, "server_client_disconnect")
	# Sent when a new client connects
	server.connect("client_connect", self, "server_client_connect")
	# Sent when a new client is ready to receive udp packets (always sent after a client connect!)
	server.connect("client_ready", self, "server_client_ready")
	# Sent a valid TCP packet is received from a client that has not been authorized yet (useful for password/auth management).
	server.connect("auth_packet", self, "server_auth_packet")
	# Sent when a valid UDP packet is received from a client
	server.connect("udp_packet", self, "server_udp")
	# Sent when a valid TCP packet is received from a client
	server.connect("tcp_packet", self, "server_tcp")
	
	# Start listening
	server.start(tcp_port,udp_port)
	
	set_process(true)

func _process(delta):
	scroll.set_v_scroll(scroll.get_v_scroll()+1000)

func server_udp(id, cmd, pkt):
	write_log(str(id) + " UDP - CMD: " + str(cmd) + " DATA:" + str(Array(pkt)))

func server_tcp(id, cmd, pkt):
	write_log(str(id) + " TCP - CMD: " + str(cmd) + " DATA:" + str(Array(pkt)))

func server_client_auth(id, cmd, pkt):
	write_log(str(id) + " - Auth packet: " + str(Array(pkt)))

func server_client_connect(id):
	write_log(str(id) + " - Connected")
	clients.append(id)
	# This function MUST be called to authorize the client
	var out = server.auth_client(id)
	write_log(out)

func server_client_ready(id):
	write_log(str(id) + " - Ready")





### Scene functions
func server_client_disconnect(id):
	write_log(str(id) + " - Disconnected")

func spawn_client():
	var spawn = client.instance()
	container.add_child(spawn)

func write_log(msg):
	print(msg)
	server_log.set_text(server_log.get_text()+"\n"+str(msg))

func _on_RemoveClient_pressed():
	if clients.size() > 0:
		var c = clients[0]
		server.kick_client(c)
		clients.remove(0)

func _on_AddClient_pressed():
	spawn_client()

func _on_BcastTCP_pressed():
	server.broadcast_tcp(RawArray([57,12,23,0]), 1)

func _on_BcastUDP_pressed():
	server.broadcast_udp(RawArray([57,12,23,0]), 3, true)
