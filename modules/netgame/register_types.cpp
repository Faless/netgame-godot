#include "register_types.h"
#include "object_type_db.h"
#include "net_game_server.h"
#include "net_game_client.h"

void register_netgame_types() {

        ObjectTypeDB::register_type<NetGameServer>();
        ObjectTypeDB::register_type<NetGameClient>();
}

void unregister_netgame_types() {
   //nothing to do here
}
