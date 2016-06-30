
extends Control

var client
var client_log
var id = 0
var connected = false
var scroll_container

func _ready():
	scroll_container = get_node("ScrollContainer")
	client_log = get_node("ScrollContainer/ClientLog")
	client = get_node("NetGameClient")
	
	# Sent when the client disconnects
	client.connect("client_disconnect", self, "client_disconnect")
	# Sent when the client connects to the TCP server
	client.connect("client_connect", self, "client_connect")
	# Sent when the new client is ready to receive udp packets (connected to the UDP server)
	client.connect("auth_packet", self, "client_auth_packet")
	# Sent when a valid UDP packet is received from the server
	client.connect("udp_packet", self, "client_udp")
	# Sent when a valid TCP packet is received from the server
	client.connect("tcp_packet", self, "client_tcp")
	
	# Connects to the given server
	client.connect_to("127.0.0.1", 4666, 4666)
	
	client_log.set_text("")
	write_log("Connecting...")
	set_process(true)

var timer = 0
func _process(delta):
	scroll_container.set_v_scroll(scroll_container.get_v_scroll()+100)

func client_udp(id, cmd, pkt):
	write_log("UDP - CMD: " + str(cmd) + " DATA:" + str(Array(pkt)))

func client_tcp(id, cmd, pkt):
	write_log("TCP - CMD: " + str(cmd) + " DATA:" + str(Array(pkt)))

func client_auth(id, cmd, pkt):
	write_log(str(id) + " - Auth packet: " + str(Array(pkt)))

func client_connect(id):
	self.id = id
	connected = true
	write_log(str(id) + " - Connect")

func client_disconnect(id):
	write_log(str(id) + " - Disconnect")



### Scene functions
func write_log(msg):
	print(msg)
	client_log.set_text(client_log.get_text()+"\n"+str(msg))

func _on_Button_pressed():
	client.put_udp_packet(RawArray([2,52,12]), 1, true)

func _on_TCP_pressed():
	client.put_tcp_packet(RawArray([22,36,89]), 1)

func _exit_tree():
	client.close()