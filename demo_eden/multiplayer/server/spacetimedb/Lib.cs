using SpacetimeDB;

// Eden multiplayer: who is on the planet and where, and the terrain edits everyone has made. Positions are
// planet-relative metres (the planet's centre is the origin), so they mean the same thing on every client.
public static partial class Module
{
    [Table(Accessor = "player", Public = true)]
    public partial struct Player
    {
        [PrimaryKey]
        public Identity identity;
        public string name;
        public bool online;
        public double x;
        public double y;
        public double z;
        // Facing, radians from planet north
        public float yaw;
        // Animation state as the client shows it: idle, walk, run, crouch_idle, crouch_walk, jump, fall, swim, tread
        public string state;
        public float speed;
    }

    [Table(Accessor = "voxel_edit", Public = true)]
    public partial struct VoxelEdit
    {
        [PrimaryKey, AutoInc]
        public ulong id;
        public Identity author;
        public double x;
        public double y;
        public double z;
        public float radius;
        // 0 dig (remove a sphere), 1 place (add a sphere painted with `material`, a V4 MAT_* index), 2 fell / break
        // the foliage there (trees, boulders)
        public byte mode;
        public byte material;
        public Timestamp at;
    }

    // A building piece (the game's EdenBuildPieces kinds): planet-relative position and rotation
    [Table(Accessor = "build_piece", Public = true)]
    public partial struct BuildPiece
    {
        [PrimaryKey, AutoInc]
        public ulong id;
        public string kind;
        public double x;
        public double y;
        public double z;
        public float qx;
        public float qy;
        public float qz;
        public float qw;
        public Identity author;
    }

    // The world's calendar clock (one row, id 0): the calendar's day count at `set_at`, and how many days pass per
    // real second (0 = paused). Clients work out the time now from it, so everyone shares one date and season.
    [Table(Accessor = "world_clock", Public = true)]
    public partial struct WorldClock
    {
        [PrimaryKey]
        public uint id;
        public double days;
        public Timestamp set_at;
        public double days_per_second;
    }

    // What this world is (one row, id 0): its name, the planet generator's seed and who created it. Set once by
    // create_world, right after the host publishes the database.
    [Table(Accessor = "world_meta", Public = true)]
    public partial struct WorldMeta
    {
        [PrimaryKey]
        public uint id;
        public string name;
        public long seed;
        public Identity owner;
        public Timestamp created_at;
    }

    // A player's inventory: item counts in the game's EdenMiner.ITEMS order. Kept between sessions like the
    // player row (where they are), so rejoining puts them back as they left.
    [Table(Accessor = "player_inventory", Public = true)]
    public partial struct PlayerInventory
    {
        [PrimaryKey]
        public Identity identity;
        public System.Collections.Generic.List<int> counts;
    }

    // The server's world directory. Only used in the database named "eden-lobby" (the same module), which the game's
    // world list reads on every server it knows: each hosted world lists itself there.
    [Table(Accessor = "world_listing", Public = true)]
    public partial struct WorldListing
    {
        [PrimaryKey]
        public string database;
        public string name;
        public long seed;
        public string host_name;
        public Identity lister;
        public Timestamp listed_at;
    }

    [Reducer(ReducerKind.Init)]
    public static void Init(ReducerContext ctx)
    {
        // Month 4, day 1, 09:00 (4-day months): the northern summer solstice, a morning
        ctx.Db.world_clock.Insert(new WorldClock { id = 0, days = 12.375, set_at = ctx.Timestamp, days_per_second = 1.0 / (24.0 * 60.0) });
    }

    // Sets the world's date and time and how fast it runs (the calendar panel's speed and skip buttons)
    [Reducer]
    public static void set_clock(ReducerContext ctx, double days, double days_per_second)
    {
        if (ctx.Db.player.identity.Find(ctx.Sender) is null)
        {
            throw new System.Exception("Join with set_name first");
        }
        if (!double.IsFinite(days) || days < 0 || !double.IsFinite(days_per_second) || days_per_second < 0 || days_per_second > 1.0)
        {
            throw new System.Exception("Bad clock");
        }
        var row = new WorldClock { id = 0, days = days, set_at = ctx.Timestamp, days_per_second = days_per_second };
        if (ctx.Db.world_clock.id.Find(0) is null)
        {
            ctx.Db.world_clock.Insert(row);
        }
        else
        {
            ctx.Db.world_clock.id.Update(row);
        }
    }

    static readonly string[] PieceKinds = { "wood_floor", "wood_wall", "wood_half_wall", "wood_beam", "wood_pole", "wood_roof",
        "wood_stairs", "stone_floor", "stone_wall", "stone_pillar" };

    const int MaxName = 24;
    const float MaxRadius = 2.0f;
    // How far from where the server last saw a player they may edit (the client's reach is 6 m)
    const double MaxEditReach = 12.0;
    static readonly string[] States = { "idle", "walk", "run", "crouch_idle", "crouch_walk", "jump", "fall", "swim", "tread" };

    // A player row exists once a client joins with set_name (the game does on connecting). Other connections,
    // such as `spacetime sql` from the CLI, don't show up as players.
    [Reducer(ReducerKind.ClientConnected)]
    public static void ClientConnected(ReducerContext ctx)
    {
        if (ctx.Db.player.identity.Find(ctx.Sender) is Player p)
        {
            p.online = true;
            ctx.Db.player.identity.Update(p);
        }
    }

    [Reducer(ReducerKind.ClientDisconnected)]
    public static void ClientDisconnected(ReducerContext ctx)
    {
        if (ctx.Db.player.identity.Find(ctx.Sender) is Player p)
        {
            p.online = false;
            ctx.Db.player.identity.Update(p);
        }
    }

    [Reducer]
    public static void set_name(ReducerContext ctx, string name)
    {
        name = name.Trim();
        if (name.Length == 0 || name.Length > MaxName)
        {
            throw new System.Exception($"Names are 1-{MaxName} characters");
        }
        if (ctx.Db.player.identity.Find(ctx.Sender) is Player p)
        {
            p.name = name;
            p.online = true;
            ctx.Db.player.identity.Update(p);
        }
        else
        {
            ctx.Db.player.Insert(new Player { identity = ctx.Sender, name = name, online = true, state = "idle" });
        }
    }

    [Reducer]
    public static void update_player(ReducerContext ctx, double x, double y, double z, float yaw, string state, float speed)
    {
        if (!double.IsFinite(x) || !double.IsFinite(y) || !double.IsFinite(z) || !float.IsFinite(yaw) || !float.IsFinite(speed))
        {
            throw new System.Exception("Bad position");
        }
        var p = ctx.Db.player.identity.Find(ctx.Sender) ?? throw new System.Exception("Join with set_name first");
        p.x = x;
        p.y = y;
        p.z = z;
        p.yaw = yaw;
        p.state = System.Array.IndexOf(States, state) >= 0 ? state : "idle";
        p.speed = System.Math.Clamp(speed, 0f, 20f);
        ctx.Db.player.identity.Update(p);
    }

    static void CheckReach(ReducerContext ctx, double x, double y, double z)
    {
        var p = ctx.Db.player.identity.Find(ctx.Sender) ?? throw new System.Exception("Join with set_name first");
        var dx = x - p.x;
        var dy = y - p.y;
        var dz = z - p.z;
        if (!double.IsFinite(x + y + z) || dx * dx + dy * dy + dz * dz > MaxEditReach * MaxEditReach)
        {
            throw new System.Exception("Out of reach");
        }
    }

    [Reducer]
    public static void place_piece(ReducerContext ctx, string kind, double x, double y, double z, float qx, float qy, float qz, float qw)
    {
        CheckReach(ctx, x, y, z);
        if (System.Array.IndexOf(PieceKinds, kind) < 0 || !float.IsFinite(qx + qy + qz + qw))
        {
            throw new System.Exception("Bad piece");
        }
        ctx.Db.build_piece.Insert(new BuildPiece { kind = kind, x = x, y = y, z = z, qx = qx, qy = qy, qz = qz, qw = qw, author = ctx.Sender });
    }

    [Reducer]
    public static void remove_piece(ReducerContext ctx, ulong id)
    {
        var piece = ctx.Db.build_piece.id.Find(id) ?? throw new System.Exception("No such piece");
        CheckReach(ctx, piece.x, piece.y, piece.z);
        ctx.Db.build_piece.id.Delete(id);
    }

    // Names the world and fixes its seed. Only the first call counts (the host's, right after publishing).
    [Reducer]
    public static void create_world(ReducerContext ctx, string name, long seed)
    {
        if (ctx.Db.world_meta.id.Find(0) is not null)
        {
            return;
        }
        name = name.Trim();
        if (name.Length == 0 || name.Length > 40)
        {
            throw new System.Exception("World names are 1-40 characters");
        }
        ctx.Db.world_meta.Insert(new WorldMeta { id = 0, name = name, seed = seed, owner = ctx.Sender, created_at = ctx.Timestamp });
    }

    const int MaxItems = 16;
    const int MaxCount = 100000;

    [Reducer]
    public static void set_inventory(ReducerContext ctx, System.Collections.Generic.List<int> counts)
    {
        if (ctx.Db.player.identity.Find(ctx.Sender) is null)
        {
            throw new System.Exception("Join with set_name first");
        }
        if (counts.Count > MaxItems || counts.Exists(c => c < 0 || c > MaxCount))
        {
            throw new System.Exception("Bad inventory");
        }
        var row = new PlayerInventory { identity = ctx.Sender, counts = counts };
        if (ctx.Db.player_inventory.identity.Find(ctx.Sender) is null)
        {
            ctx.Db.player_inventory.Insert(row);
        }
        else
        {
            ctx.Db.player_inventory.identity.Update(row);
        }
    }

    // Lists (or relists) a hosted world in this server's directory. A listing belongs to whoever made it: only they
    // can change or remove it.
    [Reducer]
    public static void list_world(ReducerContext ctx, string database, string name, long seed, string host_name)
    {
        if (database.Length == 0 || database.Length > 64 || name.Length == 0 || name.Length > 40 || host_name.Length > MaxName)
        {
            throw new System.Exception("Bad listing");
        }
        var row = new WorldListing { database = database, name = name, seed = seed, host_name = host_name, lister = ctx.Sender, listed_at = ctx.Timestamp };
        if (ctx.Db.world_listing.database.Find(database) is WorldListing old)
        {
            if (old.lister != ctx.Sender)
            {
                throw new System.Exception("Listed by someone else");
            }
            ctx.Db.world_listing.database.Update(row);
        }
        else
        {
            ctx.Db.world_listing.Insert(row);
        }
    }

    [Reducer]
    public static void unlist_world(ReducerContext ctx, string database)
    {
        var old = ctx.Db.world_listing.database.Find(database) ?? throw new System.Exception("Not listed");
        if (old.lister != ctx.Sender)
        {
            throw new System.Exception("Listed by someone else");
        }
        ctx.Db.world_listing.database.Delete(database);
    }

    [Reducer]
    public static void add_voxel_edit(ReducerContext ctx, double x, double y, double z, float radius, byte mode, byte material)
    {
        var p = ctx.Db.player.identity.Find(ctx.Sender) ?? throw new System.Exception("Join with set_name first");
        var dx = x - p.x;
        var dy = y - p.y;
        var dz = z - p.z;
        if (!double.IsFinite(x + y + z) || dx * dx + dy * dy + dz * dz > MaxEditReach * MaxEditReach)
        {
            throw new System.Exception("Out of reach");
        }
        if (mode > 2 || material > 6 || !(radius > 0.1f && radius <= MaxRadius))
        {
            throw new System.Exception("Bad edit");
        }
        ctx.Db.voxel_edit.Insert(new VoxelEdit
        {
            author = ctx.Sender, x = x, y = y, z = z, radius = radius, mode = mode, material = material, at = ctx.Timestamp,
        });
    }
}
