
/* dist: public */

#ifndef PEER_H
#define PEER_H

#define I_PEER "peer-5"

/* Only the peer module should modify any PeerArena and PeerZone instances. */

typedef struct PeerArenaName
{
	/** The name of the peer arena on this server. */
	char localName[20];

	/** The name of the peer arena on the peer server.
	* This is the same as `localName` unless an entry for it exists in Peer0:RenameArenas
	*/
	char remoteName[20];

	/** if set, the difference between localName and remoteName is only in the character casing */
	bool caseChange;
} PeerArenaName;

typedef struct PeerArena
{
	/** The arena id. This is a random integer sent to us by the peer server.
	 * This value is unique per peer server, but not across peer servers.
	 */
	u32 id;

	/** A locally generated id. This id is unique across all configured peer servers.
	 * This is used when relaying peer arenas
	 */
	u32 localId;

	PeerArenaName name;

	/** If this value is set, this arena is present in the PeerX:Arenas config value */
	int configured;

	/** If this value is set, this arena is present in the PeerX:RelayArenas config value */
	int relay;

	ticks_t lastUpdate;

	/** A list containing player names (strings). */
	LinkedList players;

	/** How many entries "players" has */
	int playerCount;
} PeerArena;

typedef struct PeerZone
{
	struct PeerZoneConfig
	{
		// [Peer3] -> 3
		int id;

		/** A peer zone is uniquely identified by its IP + port combination. */
		struct sockaddr_in sin;

		/** Each peer zone has a password configured. The CRC32 of this
		  * password is used in the protocol to validate any packets.
		  */
		u32 passwordHash;

		/* Config values: */
		int sendOnly;
		int sendPlayerList;
		int sendZeroPlayerCount;
		int sendMessages;
		int receiveMessages;
		int includeInPopulation;
		int providesDefaultArenas;

		/**
		* A list of arenas (a string) for which we will display 
		* a player count and redirect to this peer zone.
		* The value is the same as `localName` in PeerArena
		* This holds the value of PeerX:Arenas
		*/
		LinkedList arenas;

		/**
		* A list of arenas (PeerArenaName) which will be displayed
		* using an alternate name on this server in ?arena and ?go
		* This holds the value of PeerX:RenameArenas
		*/
		LinkedList renamedArenas;

		/**
		* A list of arenas (a string) for which instead of a full player list,
		* a single dummy player will be sent to this peer.
		* The value is the same as `localName` in PeerArena
		* This holds the value of PeerX:SendDummyArenas
		*/
		LinkedList sendDummyArenas;

		/**
		* A list of arenas (a string) of this peer that will be relayed to other peers.
		* This holds the value of PeerX:RelayArenas
		*/
		LinkedList relayArenas;
	} config;

	/** If the peer is not sending a player list this will hold the content of the player count
	  * packet. Otherwise this value is always -1
	  */
	int playerCount;

	/** This is a list of all the peer arenas we received from this peer zone
	 * Contains "struct PeerArena" pointers.
	 * Note that it is possible for peer zones to not send a player list (configuration option),
	 * in which case this list is empty, however you can still receive a player count and messages.
	 */
	LinkedList arenas;

	/** This is a hash table of all the peer arenas we received from this peer zone
	 * Key is arena `remoteName`, value is a "struct PeerArena" pointer.
	 */
	HashTable arenaTable;

	// A bunch of private stuff that is not very useful outside of this module
	u32 timestamps[0x100];
	int playerListBufferSize;
	u8* playerListBuffer;
} PeerZone;

typedef struct Ipeer
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/** Return the total population all peer zones
	 * @return player count
	 */
	int (*GetPopulationSummary)();
	/* pyint: string -> int */

	/** Find a player in one of our peer zones. When a partial name is given, a best guess is returned.
	 * Guesses are based on a score, 0 means perfect match. Higher values indicate how far from the start the
	 * given search string matches (see Cfind in playercmd.c).
	 * Only arenas that the peer module been configured for will be checked (PeerX:Arenas).
	 * @param findName The player name to find
	 * @param score The score this function has to beat, it is modified if a match is found
	 * @param name A buffer to place the name of the found player in
	 * @param arena A buffer to place the arena name of the player in (the localName)
	 * @param buflen The length of both the name and arena buffers
	 * @return TRUE if a match has been found
	 */
	int (*FindPlayer)(const char *findName, int *score, char *name, char *arena, int bufLen);
	/* pyint: string in, int inout, string out, string out, int buflen -> int */

	/** Attempt to place the given player in the arena of one of the peer zones.
	 * Only arenas that the peer module been configured for will be handled (PeerX:Arenas).
	 * @param p The player to place
	 * @param arenaType The arena type the client sent us in the go packet
	 * @param arena The arena name given in the go packet
	 * @return
	 */
	int (*ArenaRequest)(Player *p, int arenaType, const char *arena);
	/* pyint: player_not_none, int, string -> int */

	/** Sends a zone chat message to all peer zones (like *zone or ?z). */
	void (*SendZoneMessage)(const char *format, ...)
		ATTR_FORMAT(printf, 1, 2);
	/* pyint: formatted -> void */

	/** Sends an alert message to all connected staff in all the peer zones.
	  * This is the same as ?help and ?cheater in subgame
	  * (see Misc:AlertCommand in subgame)
	  * @param alertName The command that this alert was generated from e.g. "cheater"
	  * @param playerName
	  * @param arenaName
	  * @param format
	  */
	void (*SendAlertMessage)(const char *alertName, const char *playerName, const char *arenaName, const char *format, ...)
		ATTR_FORMAT(printf, 4, 5);
	/* pyint: string, string, string, formatted -> void */

	/** Locks the global peer zone lock.
	 * There is a lock protecting the peer zone list, which you need to hold
	 * whenever you access Ipeer::peers or the content of any of its entries.
	 * Call this before you start and Unlock when you're done.
	 */
	void (*Lock)();
	/* pyint: void -> void */

	/** Unlocks the global peer zone lock.
	 * Use this whenever you used Ipeer::Lock.
	 */
	void (*Unlock)();
	/* pyint: void -> void */

	/** Find a peer zone using the given address & port.
	 * Make sure you Lock()
	 * @param sin ip address & port to match
	 * @return The first match or NULL
	 */
	PeerZone* (*FindZone)(struct sockaddr_in *sin);

	/** Find a peer arena with the given `name`. This may also return arena's that we are not configured to
	 * display to players for.
	 * Make sure you Lock()
	 * @param arena Arena name to match
	 * @param remote If false, match using the arena `localName`; if true, match using the arena `remoteName`
	 * @return The first match or NULL
	 */
	PeerArena* (*FindArena)(const char *arenaName, bool remote);

	/** This is a list of all the peer zone we know about.
	 * Don't forget the lock.
	 * Contains "struct PeerZone" pointers
	 */
	LinkedList peers;
} Ipeer;

/** Iterate through all the peer zones we know about.
 * You'll need a Link * named "link" and an Ipeer * named "peer" in
 * the current scope. Don't forget the necessary locking.
 * @param pa the variable that will hold each successive arena
 * @see Ipeer::Lock
 */
#define FOR_EACH_PEER_ZONE(pz) \
	for ( \
		link = LLGetHead(&peer->peers); \
		link && ((pz = link->data, link = link->next) || 1); )

#endif
